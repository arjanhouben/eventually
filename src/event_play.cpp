#include <iostream>
#include <vector>
#include <fstream>

#include "ApplicationServices/ApplicationServices.h"

using namespace std;

bool error( const string &err )
{
	throw runtime_error( err );
}

template < typename T, typename D >
unique_ptr< T, D > make_unique( T *t, D d )
{
	return unique_ptr< T, D >{ t, d };
}

int main( int argc, char **argv )
{
	try
	{
		istream input( cin.rdbuf() );
		
		vector< UInt8 > buffer;
		for ( size_t size = 0; input.read( reinterpret_cast< char* >( &size ), sizeof( size ) ); )
		{
			buffer.resize( size );
			input.read( reinterpret_cast< char* >( &buffer[ 0 ] ), size );
			input.gcount() == size || error( "could not read event data" );
			
			auto data = make_unique( CFDataCreate( nullptr, &buffer[ 0 ], size ), &CFRelease );
			
			auto event = make_unique( CGEventCreateFromData( nullptr, data.get() ), &CFRelease );
			
			CGEventPost( kCGHIDEventTap, event.get() );
			
//			usleep( 1e4 );
		}
		
		cout << "done" << endl;
	}
	catch ( const exception &err )
	{
		cerr << err.what() << endl;
	}
	
	return 0;
}
