#include "moshiosbridge.h"
#include "iosclient.h"
#include "locale_utils.h"

extern "C"
int mosh_main(
    FILE *f_in, FILE *f_out, struct winsize *window_size,
    void (*state_callback)(const void *, const void *, size_t),
    void *state_callback_context,
    const char *ip, const char *port, const char *key, const char *predict_mode,
    const char *encoded_state_buffer, size_t encoded_state_size
              )
{
  //fwrite("Hello from the Bridge!\n", 22, 1, f_out);
  /* Adopt native locale */
  set_native_locale();

  string encoded_state = string(encoded_state_buffer, encoded_state_size);

  bool success = false;
  try {
    iOSClient client(
        fileno(f_in), f_out, window_size, state_callback, state_callback_context,
        ip, port, key, predict_mode, 0
        );

    client.init();

    try {
      success = client.main(encoded_state);
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
