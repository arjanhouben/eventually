#include <iostream>
#include <vector>
#include <map>
#include <fstream>

#include "ApplicationServices/ApplicationServices.h"

// TODO: sent only diff between events

using namespace std;

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
	
	for ( size_t size = 0; input.read( reinterpret_cast< char* >( &size ), sizeof( size ) ); )
	{
		buffer.resize( size );
		
		input.read( reinterpret_cast< char* >( buffer.data() ), size );

		input.gcount() == size || error( "could not read event data" );
		
		auto data = scoped( CFDataCreate( nullptr, buffer.data(), size ), &CFRelease );
		
		auto event = scoped( CGEventCreateFromData( nullptr, data ), &CFRelease );
		
		CGEventPost( kCGHIDEventTap, event );
	}
}

CGEventRef eventCallback( CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *userInfo )
{
	auto &output = *reinterpret_cast< ostream* >( userInfo );
	
	if ( type == kCGEventFlagsChanged )
	{
		CFRunLoopStop( CFRunLoopGetCurrent() );
		return nullptr;
	}
	
	auto data = scoped( CGEventCreateData( nullptr, event ), &CFRelease );
	size_t size = CFDataGetLength( data );
	auto range = CFRangeMake( 0, size );
	
	static vector< UInt8 > buffer;
	buffer.resize( size );
	CFDataGetBytes( data, range, buffer.data() );
	
	output.write( reinterpret_cast< const char* >( &size ), sizeof( size ) );
	output.write( reinterpret_cast< const char* >( buffer.data() ), buffer.size() );
	
	output.flush();
	
	return nullptr;
}

void record( ostream &output )
{
	CGDisplayHideCursor( kCGDirectMainDisplay ) && error( "could not hide cursor" );
	
	auto eventMask = ~0;
	
	auto eventTap = scoped( CGEventTapCreate( kCGHIDEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault, eventMask, eventCallback, &output ), &CFRelease );
	
	eventTap || error( "could not create event tap" );
	
	auto runLoopSource = scoped( CFMachPortCreateRunLoopSource( nullptr, eventTap, 0), &CFRelease );
	
	runLoopSource || error( "could not create run loop" );
	
	CFRunLoopAddSource( CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopCommonModes );
	
	CGEventTapEnable( eventTap, true );
	
	CFRunLoopRun();
}

using strings = vector< string >;
using iter = strings::const_iterator;
using func_type = function< void( iter&, iter) >;

// globals
bool doRecord = false;
ostream &output( cout );
istream &input( cin );
ofstream outputFile;
ifstream inputFile;
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

map< string, defaultfunc > handleArguments {
	{ "--record", setRecord },
	{ "-r", setRecord },
	{ "--output", setOutput },
	{ "-o", setOutput },
	{ "--input", setInput },
	{ "-i", setInput }
};

int main( int argc, char **argv )
{
	try
	{
		const strings args( argv + 1, argv + argc );

		for ( auto start = args.begin(), end = args.end(); start != end; )
		{
			handleArguments[ *start ]( start, end );
		}
		
		if ( doRecord )
		{
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