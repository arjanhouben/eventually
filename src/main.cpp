#include <iostream>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <iomanip>

#include "ApplicationServices/ApplicationServices.h"

// TODO: sent only diff between events

using namespace std;

using strings = vector< string >;
using iter = strings::const_iterator;
using func_type = function< void( iter&, iter) >;

enum class CommandType : uint8_t
{
	binary,
	binaryDiff,
	plain
};

// globals
enum class Mode
{
	playback,
	record,
	help
} mode = Mode::playback;
ostream &output( cout );
istream &input( cin );
ofstream outputFile;
ifstream inputFile;
struct Display;
vector< unique_ptr< Display > > displays;
Display *currentDisplay;
bool ignoreNextDelta = false;
CommandType sentDataType = CommandType::binary;

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

vector< UInt8 > eventToVector( CGEventRef event )
{
	auto data = scoped( CGEventCreateData( nullptr, event ), &CFRelease );
	uint32_t size = CFDataGetLength( data );
	auto range = CFRangeMake( 0, size );
	vector< UInt8 > result( size );
	CFDataGetBytes( data, range, result.data() );
	return result;
}

struct BinaryCommand : vector< UInt8 >
{
	BinaryCommand( CGEventRef event ) :
		vector< UInt8 >( eventToVector( event ) ) {}
};

ostream& operator << ( ostream &stream, const BinaryCommand &cmd )
{
	uint16_t size = cmd.size();
	CommandType type = CommandType::binary;
	stream.write( reinterpret_cast< const char* >( &size ), sizeof( size ) );
	stream.write( reinterpret_cast< const char* >( &type ), sizeof( type ) );
	stream.write( reinterpret_cast< const char* >( cmd.data() ), cmd.size() );
	return stream;
}

vector< UInt8 > diff( const vector< UInt8 > &old, const vector< UInt8 > &newdata )
{
	vector< UInt8 > diffdata;

	if ( old.size() == newdata.size() )
	{
		diffdata.push_back( 0 );
		UInt8 *count = &diffdata.back();
		diffdata.push_back( 0 );
		UInt8 *diff = &diffdata.back();

		for ( size_t i = 0; i < old.size(); ++i )
		{
			*diff = old[ i ] - newdata[ i ];
			if ( *diff || *count == 255 )
			{
				diffdata.push_back( 0 );
				count = &diffdata.back();
				diffdata.push_back( 0 );
				diff = &diffdata.back();
			}
			else
			{
				++(*count);
			}
		}
	}

	return diffdata;
}

vector< UInt8 > undiff( vector< UInt8 > &result, const vector< UInt8 > &newdata )
{
	auto i = result.begin();
	newdata.size() & 1 && error( "diffdata is incorrect" );
	auto d = newdata.begin();
	while ( i != result.end() && d != newdata.end() )
	{
		advance( i, *d++ );
		*i -= *d++;
	}
	return result;
}

struct BinaryDiffCommand : vector< UInt8 >
{
	BinaryDiffCommand( CGEventRef event ) :
		vector< UInt8 >( eventToVector( event ) ),
		originalSize( size() )
	{
		static map< size_t, vector< UInt8 > > cache;
		auto d = diff( cache[ size() ], *this );
		cache[ size() ] = *this;
		if ( !d.empty() )
		{
			swap( d );
		}
	}

	uint16_t originalSize;
};

ostream& operator << ( ostream &stream, const BinaryDiffCommand &cmd )
{
	uint16_t size = cmd.size();
	CommandType type = CommandType::binaryDiff;
	stream.write( reinterpret_cast< const char* >( &cmd.originalSize ), sizeof( cmd.originalSize ) );
	stream.write( reinterpret_cast< const char* >( &type ), sizeof( type ) );
	stream.write( reinterpret_cast< const char* >( &size ), sizeof( size ) );
	stream.write( reinterpret_cast< const char* >( cmd.data() ), cmd.size() );
	return stream;
}

void playback( istream &input )
{
	vector< UInt8 > buffer;

	for ( uint16_t size = 0; input.read( reinterpret_cast< char* >( &size ), sizeof( size ) ); )
	{
		CommandType type;
		input.read( reinterpret_cast< char* >( &type ), sizeof( type ) );

		switch ( type )
		{
			case CommandType::binary:
			{
				buffer.resize( size );
				input.read( reinterpret_cast< char* >( buffer.data() ), buffer.size() );
				input.gcount() == buffer.size() || error( "could not read event data" );
				break;
			}
			case CommandType::binaryDiff:
			{
				uint16_t diffsize = 0;
				input.read( reinterpret_cast< char* >( &diffsize ), sizeof( diffsize ) );
				buffer.resize( diffsize );
				input.read( reinterpret_cast< char* >( buffer.data() ), buffer.size() );
				input.gcount() == buffer.size() || error( "could not read event data" );
				if ( diffsize != size )
				{
					static map< size_t, vector< UInt8 > > cache;
					if ( cache[ size ].size() != size ) cache[ size ].resize( size );
					buffer = undiff( cache[ size ], buffer );
				}
				break;
			}
			default:
				error( "unknown commandtype: " + to_string( static_cast< uint8_t >( type ) ) );
		}

		auto data = scoped( CFDataCreate( nullptr, buffer.data(), size ), &CFRelease );

		auto event = scoped( CGEventCreateFromData( nullptr, data ), &CFRelease );

		CGEventPost( kCGHIDEventTap, event );
	}
}

void sendData( CGEventRef event, ostream &output )
{
	switch ( sentDataType )
	{
		case CommandType::binary:
			output << BinaryCommand( event );
			break;
		case CommandType::binaryDiff:
			output << BinaryDiffCommand( event );
			break;
		default:
			error( "unsupported output command" );
			break;
	}

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

struct Display;

class Boundry
{
	public:
		Boundry( Display &dest ) :
			destination( dest ) {}

		Display& destination;

		virtual ~Boundry() {}
		virtual Display* didMouseLeave( const Display&, CGPoint&, CGPoint ) const = 0;
	private:
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

	Display* didMouseLeave( CGPoint &pos, const CGPoint&delta ) const
	{
		for ( auto &b : boundries )
		{
			if ( auto result = b->didMouseLeave( *this, pos, delta ) )
			{
				return result;
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

	virtual void enterScreen( CGPoint pos, CGEventRef event )
	{
	}
};

struct HostDisplay : Display
{
	HostDisplay( double w, double h, const string &n ) : Display( w, h, n ) {}

	virtual CGEventRef handle( CGPoint&pos, CGEventRef event ) override final
	{
		pos = CGEventGetLocation( event );
		return event;
	}

	virtual void leaveScreen() override final
	{
		hideMouseHack();
	}

	virtual void enterScreen( CGPoint pos, CGEventRef event ) override final
	{
		showMouse();
		CGDisplayMoveCursorToPoint( kCGDirectMainDisplay, pos );
		CGEventSetLocation( event, pos );
		ignoreNextDelta = true;
	}
};

struct TopBoundry : Boundry
{
	TopBoundry( Display &dest ) : Boundry( dest ) {}

	Display* didMouseLeave( const Display &dsp, CGPoint &p, CGPoint d ) const override final
	{
		if ( p.y <= 1 )
		{
			if ( d.y < -50 )
			{
				p.y += dsp.height;
				return &destination;
			}
		}
		return nullptr;
	}
};

struct BottomBoundry : Boundry
{
	BottomBoundry( Display &dest ) : Boundry( dest ) {}

	Display* didMouseLeave( const Display &dsp, CGPoint &p, CGPoint d ) const override final
	{
		if ( p.y >= dsp.height - 1 )
		{
			if ( d.y > 50 )
			{
				p.y -= dsp.height;
				return &destination;
			}
		}
		return nullptr;
	}
};

struct RightBoundry : Boundry
{
	RightBoundry( Display &dest ) : Boundry( dest ) {}

	Display* didMouseLeave( const Display &dsp, CGPoint &p, CGPoint d ) const override final
	{
		if ( p.x >= dsp.width - 1 )
		{
			if ( d.x > 50 )
			{
				p.x -= dsp.width;
				return &destination;
			}
		}
		return nullptr;
	}
};

struct LeftBoundry : Boundry
{
	LeftBoundry( Display &dest ) : Boundry( dest ) {}

	Display* didMouseLeave( const Display &dsp, CGPoint &p, CGPoint d ) const override final
	{
		if ( p.x < 1 )
		{
			if ( d.x < -50 )
			{
				p.x += dsp.width;
				return &destination;
			}
		}
		return nullptr;
	}
};

void onDisplayChange( CGDirectDisplayID, CGDisplayChangeSummaryFlags, void *rawdisplay )
{
	auto rect = CGDisplayBounds( kCGDirectMainDisplay );
	auto display = static_cast< Display* >( rawdisplay );
	display->width = rect.size.width;
	display->height = rect.size.height;
}

CGEventRef eventCallback( CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* )
{
	static CGPoint pos = CGEventGetLocation( event );

	switch ( type )
	{
		case kCGEventMouseMoved:
		case kCGEventLeftMouseDragged:
		case kCGEventRightMouseDragged:
		{
			CGPoint delta {
				CGEventGetDoubleValueField( event, kCGMouseEventDeltaX ),
				CGEventGetDoubleValueField( event, kCGMouseEventDeltaY )
			};

			if ( ignoreNextDelta )
			{
				ignoreNextDelta = false;
				delta.x = 0;
				delta.y = 0;
			}

			pos.x += delta.x;
			pos.y += delta.y;

			if ( Display *newDisplay = currentDisplay->didMouseLeave( pos, delta ) )
			{
				currentDisplay->leaveScreen();
				currentDisplay = newDisplay;
				currentDisplay->enterScreen( pos, event );
//				cerr << "switch to " << currentDisplay->name << " display" << endl;
			}
			break;
		}
		default:
			break;
	}

	return currentDisplay->handle( pos, event );
}

void record( ostream &output )
{
	auto eventMask = ~0;

	auto eventTap = scoped( CGEventTapCreate( kCGHIDEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault, eventMask, eventCallback, &output ), &CFRelease );

	eventTap || error( "could not create event tap" );

	auto runLoopSource = scoped( CFMachPortCreateRunLoopSource( nullptr, eventTap, 0 ), &CFRelease );

	runLoopSource || error( "could not create run loop" );

	CFRunLoopAddSource( CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopCommonModes );

	CGEventTapEnable( eventTap, true );

	CFRunLoopRun();
}

const func_type error_function = []( iter &a,iter )
{
	error( "unknown argument: " + *a );
};

struct defaultfunc : func_type
{
	template < typename T = func_type >
	defaultfunc( T t = error_function, string txt = string() ) :
		func_type( t ),
		help( txt ) {}

	string help;
};

void setRecord( iter &i, iter )
{
	++i;
	mode = Mode::record;
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

void setOutputType( iter &type, iter end )
{
	const string options( "binary, bdiff and plain" );

	++type != end || error( "please specify which output type you which to use, choose from " + options );

	static map< string, CommandType > lookup {
		{ "binary", CommandType::binary },
		{ "bdiff", CommandType::binaryDiff },
		{ "plain", CommandType::plain }
	};

	switch ( lookup[ *type ] )
	{
		case CommandType::binary:
		case CommandType::binaryDiff:
		case CommandType::plain:
			sentDataType = lookup[ *type ];
			break;
		default:
			error( "unknown outputtype \'" + *type + "\', please choose from " + options );
			break;
	}

	++type;
}


void showHelp( iter &i, iter end )
{
	mode = Mode::help;
	i = end;
}

const string example( "example:~$ " );

string setScreenHelp( Border side )
{
	string name;
	switch ( side )
	{
		case Border::Top:
			name = "top";
			break;
		case Border::Right:
			name = "right";
			break;
		case Border::Bottom:
			name = "bottom";
			break;
		case Border::Left:
			name = "left";
			break;
	}
	return "add an 'output' screen on " + name + " of the host window with specified width and height.";
}

map< string, defaultfunc > commandlineOptions {
	{ "--help", { showHelp, "show this info" } },
	{ "-h", { showHelp, "shorthand for --help" } },
	{ "--record", { setRecord, "use this to record and then serialize events\n( you need to be root in order to capture keystrokes )" } },
	{ "-r", { setRecord, "shorthand for --record" } },
	{ "--output", { setOutput, "determine where the serialized output should go, defaults to stdout" } },
	{ "-o", { setOutput, "shorthand for --output" } },
	{ "--input", { setInput, "determines from where the serialized input should be read, defaults to stdin" } },
	{ "-i", { setInput, "shorthand for --input" } },
	{ "-top", { setScreen( Border::Top ), setScreenHelp( Border::Top ) } },
	{ "-right", { setScreen( Border::Right ), setScreenHelp( Border::Right ) } },
	{ "-bottom", { setScreen( Border::Bottom ), setScreenHelp( Border::Bottom ) } },
	{ "-left", { setScreen( Border::Left ), setScreenHelp( Border::Left ) } },
	{ "-type",
		{
			setOutputType,
			"determines the type of output generated by serializing the events.\n"
			"possible options are binary(default), bdiff and plain\n"
			"bdiff sents only the difference between events and should therefore minimize\n"
			"network usage, which makes it more suited for slow connections\n"
		}
	}
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

	CGDisplayRegisterReconfigurationCallback( onDisplayChange, currentDisplay );
}

int main( int argc, char **argv )
{
	try
	{
		const strings args( argv + 1, argv + argc );

		setupHostDisplay();

		for ( auto start = args.begin(), end = args.end(); start != end; )
		{
			commandlineOptions[ *start ]( start, end );
		}

		switch ( mode )
		{
			case Mode::record:
			{
				signal( SIGHUP, signalhandler ) && error( "can't catch SIGHUP" );
				signal( SIGINT, signalhandler ) && error( "can't catch SIGINT" );
				signal( SIGQUIT, signalhandler ) && error( "can't catch SIGQUIT" );
				signal( SIGABRT, signalhandler ) && error( "can't catch SIGABRT" );
				signal( SIGTERM, signalhandler ) && error( "can't catch SIGTERM" );

				record( output );
				break;
			}
			case Mode::playback:
			{
				playback( input );
				break;
			}
			case Mode::help:
			{
				for ( auto &commandlineOption : commandlineOptions )
				{
					cout << setw( 10 ) << left << commandlineOption.first << " : " << commandlineOption.second.help << endl;
				}

				cout << "\nsome examples:\n"
					<< "# start listening for events on stdin, and play them back on this machine\n"
					<< "eventually\n\n"
					<< "# add a virtual screen left of the host, with a size of 1400 by 900.\n"
					<< "# events are recorded to a file named 'events'\n"
					<< "eventually -r -left 1400 900 -o events\n\n"
					<< "# add a virtual screen left of the host, with a size of 1400 by 900.\n"
					<< "# events are recorded to stdout and then piped through ssh, which starts\n"
					<< "# an instance of eventually on the client which then executes the events\n"
					<< "eventually -r -left 1400 900 | ssh <client_ip> \"eventually\"\n\n"
					<< "# do note however, that in order to capture keystrokes, the recording end needs\n"
					<< "# to be run as root, so the commandline will become:\n"
					<< "sudo eventually -r -left 1400 900 | ssh <client_ip> \"eventually\"\n\n"
					<< endl;

				break;
			}
		}
	}
	catch ( const exception &err )
	{
		cerr << err.what() << endl;
	}

	return 0;
}
