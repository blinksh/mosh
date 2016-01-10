#include "config.h"

#include <errno.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <pwd.h>
#include <signal.h>
#include <time.h>

#include "terminalbridge.h"

#include "locale_utils.h"

using namespace std;

void TerminalBridge::init( void )
{
  if ( !is_utf8_locale() ) {
    LocaleVar native_ctype = get_ctype();
    string native_charset( locale_charset() );

    fprintf( stderr, "mosh-client needs a UTF-8 native locale to run.\n\n" );
    fprintf( stderr, "Unfortunately, the client's environment (%s) specifies\nthe character set \"%s\".\n\n", native_ctype.str().c_str(), native_charset.c_str() );
    int unused __attribute((unused)) = system( "locale" );
    exit( 1 );
  }

  // Add our name to window title
  if ( !getenv( "MOSH_TITLE_NOPREFIX" ) ) {
    overlays.set_title_prefix( wstring( L"[mosh] " ) );
  }

  /* Set terminal escape key. */
  const char *escape_key_env;
  if ( (escape_key_env = getenv( "MOSH_ESCAPE_KEY" )) != NULL ) {
    if ( strlen( escape_key_env ) == 1 ) {
      escape_key = (int)escape_key_env[0];
      if ( escape_key > 0 && escape_key < 128 ) {
	if ( escape_key < 32 ) {
	  /* If escape is ctrl-something, pass it with repeating the key without ctrl. */
	  escape_pass_key = escape_key + (int)'@';
	} else {
	  /* If escape is something else, pass it with repeating the key itself. */
	  escape_pass_key = escape_key;
	}
	if ( escape_pass_key >= 'A' && escape_pass_key <= 'Z' ) {
	  /* If escape pass is an upper case character, define optional version
	     as lower case of the same. */
	  escape_pass_key2 = escape_pass_key + (int)'a' - (int)'A';
	} else {
	  escape_pass_key2 = escape_pass_key;
	}
      } else {
	escape_key = 0x1E;
	escape_pass_key = '^';
	escape_pass_key2 = '^';
      }
    } else if ( strlen( escape_key_env ) == 0 ) {
      escape_key = -1;
    } else {
      escape_key = 0x1E;
      escape_pass_key = '^';
      escape_pass_key2 = '^';
    }
  } else {
    escape_key = 0x1E;
    escape_pass_key = '^';
    escape_pass_key2 = '^';
  }

  /* There are so many better ways to shoot oneself into leg than
     setting escape key to Ctrl-C, Ctrl-D, NewLine, Ctrl-L or CarriageReturn
     that we just won't allow that. */
  if ( escape_key == 0x03 || escape_key == 0x04 || escape_key == 0x0A || escape_key == 0x0C || escape_key == 0x0D ) {
    escape_key = 0x1E;
    escape_pass_key = '^';
    escape_pass_key2 = '^';
  }

  /* Adjust escape help differently if escape is a control character. */
  if ( escape_key > 0 ) {
    char escape_pass_name_buf[16];
    char escape_key_name_buf[16];
    snprintf(escape_pass_name_buf, sizeof escape_pass_name_buf, "\"%c\"", escape_pass_key);
    if (escape_key < 32) {
      snprintf(escape_key_name_buf, sizeof escape_key_name_buf, "Ctrl-%c", escape_pass_key);
      escape_requires_lf = false;
    } else {
      snprintf(escape_key_name_buf, sizeof escape_key_name_buf, "\"%c\"", escape_key);
      escape_requires_lf = true;
    }
    string tmp;
    tmp = string( escape_pass_name_buf );
    wstring escape_pass_name = wstring(tmp.begin(), tmp.end());
    tmp = string( escape_key_name_buf );
    wstring escape_key_name = wstring(tmp.begin(), tmp.end());
    escape_key_help = L"Commands: Ctrl-Z suspends, \".\" quits, " + escape_pass_name + L" gives literal " + escape_key_name;
    overlays.get_notification_engine().set_escape_key_string( tmp );
  }
  wchar_t tmp[ 128 ];
  swprintf( tmp, 128, L"Nothing received from server on UDP port %s.", port.c_str() );
  connecting_notification = wstring( tmp );
}

void TerminalBridge::shutdown( void )
{
  /* Restore screen state */
  overlays.get_notification_engine().set_notification_string( wstring( L"" ) );
  overlays.get_notification_engine().server_heard( Network::timestamp() );
  overlays.set_title_prefix( wstring( L"" ) );
  output_new_frame();

  if ( still_connecting() ) {
    fprintf( stderr, "\nmosh did not make a successful connection to %s:%s.\n", ip.c_str(), port.c_str() );
    fprintf( stderr, "Please verify that UDP port %s is not firewalled and can reach the server.\n\n", port.c_str() );
    fprintf( stderr, "(By default, mosh uses a UDP port between 60000 and 61000. The -p option\nselects a specific UDP port number.)\n" );
  } else if ( network ) {
    if ( !clean_shutdown ) {
      fprintf( stderr, "\n\nmosh did not shut down cleanly. Please note that the\nmosh-server process may still be running on the server.\n" );
    }
  }
}

void TerminalBridge::output_new_frame() {
}
