#include "iosclient.h"

extern "C"
int mosh_main(FILE *f_in, FILE *f_out,
	      const char *ip, const char *port, const char *key, const char *predict_mode)
{
  fwrite("Hello from the Bridge!\n", 22, 1, f_out);
  bool success = false;
  try {
    iOSClient client(fileno(f_in), f_out, 
		     ip, port, key, predict_mode);
    // STMClient client( ip.c_str(), port.c_str(), 
    // 		      strdup(key.c_str()), predict.c_str() );
    client.init();

    try {
      success = client.main();
    } catch ( ... ) {
      client.shutdown();
      throw;
    }

    client.shutdown();
  } catch ( const Network::NetworkException &e ) {
    fprintf( stderr, "Network exception: %s\r\n",
	     e.what() );
    success = false;
  } catch ( const Crypto::CryptoException &e ) {
    fprintf( stderr, "Crypto exception: %s\r\n",
	     e.what() );
    success = false;
  } catch ( const std::exception &e ) {
    fprintf( stderr, "Error: %s\r\n", e.what() );
    success = false;
  }
  
  return !success;
}
