#include <iostream>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>

#include "ApplicationServices/ApplicationServices.h"

// TODO: sent only diff between events

using namespace std;


using strings = vector< string >;
using iter = strings::const_iterator;
using func_type = function< void( iter&, iter) >;

// globals
bool doRecord = false;
ostream &output( cout );
istream &input( cin );
ofstream outputFile;
ifstream inputFile;
struct Display;
vector< unique_ptr< Display > > displays;
Display *currentDisplay;

bool error( const string &err )
{
	throw runtime_error( err );
}

template < typename T, typename D >
struct scoped_resource : unique_ptr< T, D >
{
	scoped_resource( T *t, D d) : unique_ptr< T, D >{ t, d } {}
	operator T*() { return this->get(); }
};

template < typename T, typename D >
scoped_resource< T, D > scoped( T *t, D d )
{
	return scoped_resource< T, D >{ t, d };
}

void playback( istream &input )
{
	vector< UInt8 > buffer;
	
	for ( uint32_t size = 0; input.read( reinterpret_cast< char* >( &size ), sizeof( size ) ); )
	{
		buffer.resize( size );
		
		input.read( reinterpret_cast< char* >( buffer.data() ), size );

		input.gcount() == size || error( "could not read event data" );
		
		auto data = scoped( CFDataCreate( nullptr, buffer.data(), size ), &CFRelease );
		
		auto event = scoped( CGEventCreateFromData( nullptr, data ), &CFRelease );
		
		CGEventPost( kCGHIDEventTap, event );
	}
}

void sendData( CGEventRef event, ostream &output )
{
	auto data = scoped( CGEventCreateData( nullptr, event ), &CFRelease );
	uint32_t size = CFDataGetLength( data );
	auto range = CFRangeMake( 0, size );
	
	static vector< UInt8 > buffer;
	buffer.resize( size );
	CFDataGetBytes( data, range, buffer.data() );
	
	output.write( reinterpret_cast< const char* >( &size ), sizeof( size ) );
	output.write( reinterpret_cast< const char* >( buffer.data() ), buffer.size() );
	
	output.flush();
}

ostream& operator << ( ostream &stream, CGPoint p )
{
	return stream << p.x << 'x' << p.y;
}

template < typename T >
inline double clamp( T v, T mi, T ma )
{
	return min( ma, max( mi, v ) );
}

typedef size_t CGSConnectionID;
extern "C" CGSConnectionID _CGSDefaultConnection();
extern "C" void CGSSetConnectionProperty( CGSConnectionID, CGSConnectionID, CFStringRef, CFBooleanRef );

void hideMouseHack()
{
    auto propertyString = scoped( CFStringCreateWithCString( nullptr, "SetsCursorInBackground", kCFStringEncodingUTF8 ), &CFRelease );
    CGSSetConnectionProperty( _CGSDefaultConnection(), _CGSDefaultConnection(), propertyString, kCFBooleanTrue );
	
    CGDisplayHideCursor( kCGDirectMainDisplay );
}

void showMouse()
{
    auto propertyString = scoped( CFStringCreateWithCString( nullptr, "SetsCursorInBackground", kCFStringEncodingUTF8 ), &CFRelease );
    CGSSetConnectionProperty( _CGSDefaultConnection(), _CGSDefaultConnection(), propertyString, kCFBooleanFalse );
	
    CGDisplayShowCursor( kCGDirectMainDisplay );
}

void onDisplayChange()
{
//	uint32_t displayCount = 0;
//	CGGetOnlineDisplayList( 0, nullptr, &displayCount ) && error( "could not get display count" );
//	displays.resize( displayCount );
//	CGGetOnlineDisplayList( displays.size(), displays.data(), &displayCount ) && error( "could not get display count" );
}

void onDisplayChange( CGDirectDisplayID, CGDisplayChangeSummaryFlags, void* )
{
	onDisplayChange();
}

struct Display;

class Boundry
{
	public:
		Boundry( Display &dest ) :
			dest_( dest ) {}
	
		Display& destination() const
		{
			return dest_;
		}
	
		virtual ~Boundry() {}
		virtual bool isMouseOn( const Display &, CGPoint ) const = 0;
		virtual Display* didMouseLeave( CGPoint, CGPoint ) const = 0;
	private:
		
		Display &dest_;
};

struct Display
{
	Display( double w, double h, const string &n ) :
		width( w ),
		height( h ),
		name( n ),
		boundries() {}
	
	double width, height;
	string name;
	vector< unique_ptr< Boundry > > boundries;
	
	virtual ~Display() {}
	
	virtual CGEventRef handle( CGPoint &p, CGEventRef event )
	{
		CGEventSetLocation( event, clamp( p ) );
		sendData( event, output );
		return nullptr;
	}
	
	Display* didMouseLeave( const CGPoint &pos, const CGPoint&delta ) const
	{
		for ( auto &b : boundries )
		{
			if ( b->isMouseOn( *this, pos ) )
			{
				if ( auto result = b->didMouseLeave( pos, delta ) )
				{
					return result;
				}
			}
		}
		return nullptr;
	}
	
	CGPoint clamp( CGPoint &p ) const
	{
		p.x = ::clamp< double >( p.x, 0, width );
		p.y = ::clamp< double >( p.y, 0, height );
		return p;
	}
	
	virtual void leaveScreen()
	{
	}
	
	virtual void enterScreen()
	{
	}
};

struct HostDisplay : Display
{
	HostDisplay( double w, double h, const string &n ) : Display( w, h, n ) {}
	
	virtual CGEventRef handle( CGPoint&, CGEventRef event ) override final
	{
		return event;
	}
	
	virtual void leaveScreen() override final
	{
		hideMouseHack();
	}
	
	virtual void enterScreen() override final
	{
		showMouse();
	}
};

struct TopBoundry : Boundry
{
	TopBoundry( Display &dest ) : Boundry( dest ) {}
	
	bool isMouseOn( const Display &dsp, CGPoint p ) const override final
	{
		return p.y <= 1;
	}
	
	Display* didMouseLeave( CGPoint, CGPoint d ) const override final
	{
		return ( d.y < -50 ) ? &destination() : nullptr;
	}
};

struct BottomBoundry : Boundry
{
	BottomBoundry( Display &dest ) : Boundry( dest ) {}
	
	bool isMouseOn( const Display &dsp, CGPoint p ) const override final
	{
		return p.y >= dsp.height - 1;
	}
	
	Display* didMouseLeave( CGPoint, CGPoint d ) const override final
	{
		return ( d.y > 50 ) ? &destination() : nullptr;
	}
};

struct RightBoundry : Boundry
{
	RightBoundry( Display &dest ) : Boundry( dest ) {}
	
	bool isMouseOn( const Display &dsp, CGPoint p ) const override final
	{
		return p.x >= dsp.width - 1;
	}
	
	Display* didMouseLeave( CGPoint, CGPoint d ) const override final
	{
		return ( d.x > 50 ) ? &destination() : nullptr;
	}
};

struct LeftBoundry : Boundry
{
	LeftBoundry( Display &dest ) : Boundry( dest ) {}
	
	bool isMouseOn( const Display &dsp, CGPoint p ) const override final
	{
		return p.x < 1;
	}
	
	Display* didMouseLeave( CGPoint, CGPoint d ) const override final
	{
		return ( d.x < -50 ) ? &destination() : nullptr;
	}
};

CGEventRef eventCallback( CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* )
{
	auto key = CGEventGetIntegerValueField( event, kCGKeyboardEventKeycode );
	
	if ( key == 63 )
	{
		CFRunLoopStop( CFRunLoopGetCurrent() );
		return nullptr;
	}
	
	const auto location = CGEventGetLocation( event );
	
	static CGPoint pos = location;
	
	CGPoint delta {
		CGEventGetDoubleValueField( event, kCGMouseEventDeltaX ),
		CGEventGetDoubleValueField( event, kCGMouseEventDeltaY )
	};
	
	pos.x += delta.x;
	pos.y += delta.y;
	
#if 0
	CGEventSetLocation( event, pos );
	sendData( event, output );
	return nullptr;
#endif
	
	if ( Display *newDisplay = currentDisplay->didMouseLeave( pos, delta ) )
	{
		currentDisplay->leaveScreen();
		currentDisplay = newDisplay;
		currentDisplay->enterScreen();
		
		cerr << "switch to display " << currentDisplay->name << endl;
		pos.x = currentDisplay->width / 2;
		pos.y = currentDisplay->height / 2;
	}

	return currentDisplay->handle( pos, event );
}

void record( ostream &output )
{
	auto eventMask = ~0;
	
	auto eventTap = scoped( CGEventTapCreate( kCGHIDEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault, eventMask, eventCallback, &output ), &CFRelease );
	
	eventTap || error( "could not create event tap" );
	
	auto runLoopSource = scoped( CFMachPortCreateRunLoopSource( nullptr, eventTap, 0), &CFRelease );
	
	runLoopSource || error( "could not create run loop" );
	
	CFRunLoopAddSource( CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopCommonModes );
	
	CGEventTapEnable( eventTap, true );
	
	CFRunLoopRun();
	
	CGDisplayShowCursor( kCGDirectMainDisplay );
}

const func_type error_function = []( iter &a,iter )
{
	error( "unknown argument: " + *a );
};

struct defaultfunc : func_type
{
	template < typename T = func_type >
	defaultfunc( T t = error_function ) : func_type( t ) {}
};

void setRecord( iter &i, iter )
{
	++i;
	doRecord = true;
}

void setOutput( iter &file, iter end )
{
	++file != end || error( "please supply filename for output" );
	
	outputFile = ofstream( *file++, ios::binary );

	output.rdbuf( outputFile.rdbuf() );
}

void setInput( iter &file, iter end )
{
	++file != end || error( "please supply filename for input" );
	
	inputFile = ifstream( *file++, ios::binary );
	input.rdbuf( inputFile.rdbuf() );
}

Display& getDisplay( iter &size, iter end, const string &name )
{
	++size != end || error( "please supply size for " + name + " screen ( <width> <height> )" );
	
	double w = 0, h = 0;
	
	istringstream( *size++ ) >> w;
	w >= 640 || error( "width has to be at least 640, was " + to_string( w ) );
	
	istringstream( *size++ ) >> h;
	h >= 480 || error( "height has to be at least 480, was " + to_string( h ) );
	
	displays.push_back( unique_ptr< Display >( new Display{ w, h, name } ) );
	
	return *displays.back();
}

void setTopScreen( Display &display )
{
	currentDisplay->boundries.push_back( unique_ptr< Boundry >( new TopBoundry( display ) ) );
	display.boundries.push_back( unique_ptr< Boundry >( new BottomBoundry( *currentDisplay ) ) );
}

enum class Border
{
	Top,
	Right,
	Bottom,
	Left
};

function< void( iter &size, iter end ) > setScreen( Border b )
{
	switch ( b )
	{
		case Border::Top:
			return []( iter &size, iter end )
			{
				auto &d = getDisplay( size, end, "top" );
				currentDisplay->boundries.push_back( unique_ptr< Boundry >( new TopBoundry( d ) ) );
				d.boundries.push_back( unique_ptr< Boundry >( new BottomBoundry( *currentDisplay ) ) );
			};
		case Border::Right:
			return []( iter &size, iter end )
			{
				auto &d = getDisplay( size, end, "right" );
				currentDisplay->boundries.push_back( unique_ptr< Boundry >( new RightBoundry( d ) ) );
				d.boundries.push_back( unique_ptr< Boundry >( new LeftBoundry( *currentDisplay ) ) );
			};
		case Border::Bottom:
			return []( iter &size, iter end )
			{
				auto &d = getDisplay( size, end, "bottom" );
				currentDisplay->boundries.push_back( unique_ptr< Boundry >( new BottomBoundry( d ) ) );
				d.boundries.push_back( unique_ptr< Boundry >( new TopBoundry( *currentDisplay ) ) );
			};
		case Border::Left:
			return []( iter &size, iter end )
			{
				auto &d = getDisplay( size, end, "left" );
				currentDisplay->boundries.push_back( unique_ptr< Boundry >( new LeftBoundry( d ) ) );
				d.boundries.push_back( unique_ptr< Boundry >( new RightBoundry( *currentDisplay ) ) );
			};
	}
}

map< string, defaultfunc > handleArguments {
	{ "--record", setRecord },
	{ "-r", setRecord },
	{ "--output", setOutput },
	{ "-o", setOutput },
	{ "--input", setInput },
	{ "-i", setInput },
	{ "-top", setScreen( Border::Top ) },
	{ "-right", setScreen( Border::Right ) },
	{ "-bottom", setScreen( Border::Bottom ) },
	{ "-left", setScreen( Border::Left ) },
};

void signalhandler( int )
{
	CFRunLoopStop( CFRunLoopGetCurrent() );
}

void setupHostDisplay()
{
	auto rect = CGDisplayBounds( kCGDirectMainDisplay );
	displays.push_back( unique_ptr< Display >( new HostDisplay{ rect.size.width, rect.size.height, "host" } ) );
	currentDisplay = displays.back().get();
}

int main( int argc, char **argv )
{
	try
	{
		const strings args( argv + 1, argv + argc );
		
		setupHostDisplay();

		for ( auto start = args.begin(), end = args.end(); start != end; )
		{
			handleArguments[ *start ]( start, end );
		}
		
		if ( doRecord )
		{
			signal( SIGTERM, signalhandler );
			signal( SIGSTOP, signalhandler );
			signal( SIGABRT, signalhandler );
			signal( SIGQUIT, signalhandler );
			signal( SIGKILL, signalhandler );
			
			CGDisplayRegisterReconfigurationCallback( onDisplayChange, nullptr );
			onDisplayChange();
			
			record( output );
		}
		else
		{
			playback( input );
		}
	}
	catch ( const exception &err )
	{
		cerr << err.what() << endl;
	}
	
	return 0;
}