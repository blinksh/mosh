//   Mosh: the mobile shell
//   Copyright 2012 Keith Winstein
//
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "config.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
//#include <vector>
//#include <map>
#include <stdio.h>
#include <string>
#include <sys/socket.h>
#include <getopt.h>
 #include <arpa/inet.h>
// #include <netdb.h>
//#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <libssh2.h>
//#include <sys/ioctl.h>
//#include <sys/types.h>
//#include <sys/wait.h>

#include <sstream>
#include <iostream>

#include "stmclient.h"
#include "terminalbridge.h"
#include "locale_utils.h"

using namespace std;

void die( const char *format, ... ) {
  va_list args;
  va_start( args, format );
  vfprintf( stderr, format, args );
  va_end( args );
  fprintf( stderr, "\n" );
  exit( 255 );
}

static const char *usage_format =
"Usage: %s [options] [--] [user@]host [command...]\n"
"        --client=PATH        mosh client on local machine\n"
"                                (default: mosh-client)\n"
"        --server=PATH        mosh server on remote machine\n"
"                                (default: mosh-server)\n"
"\n"
"        --predict=adaptive   local echo for slower links [default]\n"
"-a      --predict=always     use local echo even on fast links\n"
"-n      --predict=never      never use local echo\n"
"\n"
"-p NUM  --port=NUM           server-side UDP port\n"
"\n"
"        --ssh=COMMAND        ssh command to run when setting up session\n"
"                                (example: \"ssh -p 2222\")\n"
"                                (default: \"ssh\")\n"
"\n"
"        --help               this message\n"
"        --version            version and copyright information\n"
"\n"
"Please report bugs to mosh-devel@mit.edu.\n"
"Mosh home page: http://mosh.mit.edu";

static const char *version_format =
"mosh %s\n"
"Copyright 2012 Keith Winstein <mosh-devel@mit.edu>\n"
"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
"This is free software: you are free to change and redistribute it.\n"
"There is NO WARRANTY, to the extent permitted by law.";

static const char *key_valid_char_set =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/+";

static char *argv0;

void predict_check( const string &predict, bool env_set )
{
  if ( predict != "adaptive" &&
       predict != "always" &&
       predict != "never" ) {
    fprintf( stderr, "%s: Unknown mode \"%s\"%s.\n", argv0, predict.c_str(),
        env_set ? " (MOSH_PREDICTION_DISPLAY in environment)" : "" );
    die( usage_format, argv0 );
  }
}

void cat( int ifd, int ofd )
{
  char buf[4096];
  ssize_t n;
  while ( 1 ) {
    n = read( ifd, buf, sizeof( buf ) );
    if ( n==-1 ) {
      if (errno == EINTR ) {
        continue;
      }
      break;
    }
    if ( n==0 ) {
      break;
    }
    n = write( ofd, buf, n );
    if ( n==-1 ) {
      break;
    }
  }
}

static int waitsocket(int socket_fd, LIBSSH2_SESSION *session)
{
  struct timeval timeout;
  int rc;
  fd_set fd;
  fd_set *writefd = NULL;
  fd_set *readfd = NULL;
  int dir;

  timeout.tv_sec = 10;
  timeout.tv_usec = 0;

  FD_ZERO(&fd);

  FD_SET(socket_fd, &fd);

  /* now make sure we wait in the correct direction */
  dir = libssh2_session_block_directions(session);

  if(dir & LIBSSH2_SESSION_BLOCK_INBOUND)
    readfd = &fd;

  if(dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
    writefd = &fd;

  rc = select(socket_fd + 1, readfd, writefd, NULL, &timeout);

  return rc;
}

int main( int argc, char *argv[] )
{
  argv0 = argv[0];
  // TODO: Make constants
  string client = "mosh-client";
  string server = "mosh-server";
  string ssh = "ssh";
  string predict, port_request;
  int help=0, version=0, fake_proxy=0;

  static struct option long_options[] =
  {
    { "client",      required_argument,  0,              'c' },
    { "server",      required_argument,  0,              's' },
    { "predict",     required_argument,  0,              'r' },
    { "port",        required_argument,  0,              'p' },
    { "ssh",         required_argument,  0,              'S' },
    { "help",        no_argument,        &help,           1  },
    { "version",     no_argument,        &version,        1  },
    { "fake-proxy!", no_argument,        &fake_proxy,     1  },
    { 0, 0, 0, 0 }
  };

  // TODO: Bootstrap func with opt parameters
  while ( 1 ) {
    int option_index = 0;
    int c = getopt_long( argc, argv, "anp:",
        long_options, &option_index );
    if ( c == -1 ) {
      break;
    }

    switch ( c ) {
      case 0:
        // flag has been set
        break;
      case 'c':
        client = optarg;
        break;
      case 's':
        server = optarg;
        break;
      case 'r':
        predict = optarg;
        break;
      case 'p':
        port_request = optarg;
        break;
      case 'S':
        ssh = optarg;
        break;
      case 'a':
        predict = "always";
        break;
      case 'n':
        predict = "never";
        break;
      default:
        die( usage_format, argv[0] );
    }
  }

  if ( help ) {
    die( usage_format, argv[0] );
  }
  if ( version ) {
    die( version_format, PACKAGE_VERSION );
  }
  // TODO: Force predictive to adaptive
  if ( predict.size() ) {
    predict_check( predict, 0 );
  } else if ( getenv( "MOSH_PREDICTION_DELAY" ) ) {
    predict = getenv( "MOSH_PREDICTION_DELAY" );
    predict_check( predict, 1 );
  } else {
    predict = "adaptive";
    predict_check( predict, 0 );
  }
  
  // TODO: Accept port request from value on field
  if ( port_request.size() ) {
    if ( port_request.find_first_not_of( "0123456789" ) != string::npos ||
         atoi( port_request.c_str() ) < 0 ||
         atoi( port_request.c_str() ) > 65535 ) {
      die( "%s: Server-side port (%s) must be within valid range [0..65535].",
           argv[0],
           port_request.c_str() );
    }
  }

  unsetenv( "MOSH_PREDICTION_DISPLAY" );

  //string userhost = argv[optind++];
  // Create ssh connection with those parameters
  struct sockaddr_in sin;
  int sock;

  const char *host = "127.0.0.1";
  LIBSSH2_SESSION *session;
  LIBSSH2_CHANNEL *channel;
  int rc;  
  unsigned long hostaddr;

  hostaddr = inet_addr(host);

  // Connect to port on host
  sock = socket(AF_INET, SOCK_STREAM, 0);
  sin.sin_family = AF_INET;
  sin.sin_port = htons(22); // TODO: Use a custom ssh port
  sin.sin_addr.s_addr = hostaddr; // !! Always an IP address

  if ((rc = connect(sock, (struct sockaddr*)(&sin),
		    sizeof(struct sockaddr_in))) != 0) {
    die("failed to open socket for ssh connection!\n");
  }

  session = libssh2_session_init();
  if (!session) {
    die("create session failed");
  }

  // tell libssh2 we want it all done none non-blocking
  libssh2_session_set_blocking(session, 0);
  
  // ... start it up. This will trade welcome banners, exchange keys,
  // and setup crypto, compression, and MAC layers  
  while ((rc = libssh2_session_handshake(session, sock)) == 
	 LIBSSH2_ERROR_EAGAIN);
  if (rc) {
    die("Session handshake failed");
  }

  const char *username = "simple";
  const char *password = "n0th1ng-more.";

  //ignore knownhosts, etc... although we should save them on a file
  //libssh2_knownhost_init(...) [..]
  
  while ((rc = libssh2_userauth_password(session, username, password)) ==

  	 LIBSSH2_ERROR_EAGAIN);
  if (rc) {
    die("Wrong password");
  }
  // while ((rc = libssh2_userauth_publickey_fromfile(session, username,
  // 						   "/home/carlos/.ssh/id_rsa.pub",
  // 						   "/home/carlos/.ssh/id_rsa",
  // 						   password)) == LIBSSH2_ERROR_EAGAIN);
  // if (rc) {
  //   die("Authentication failed");
  // }
  
  /* Exec non-blocking on the remote host */
  while( (channel = libssh2_channel_open_session(session)) == NULL &&
	 libssh2_session_last_error(session,NULL,NULL,0) ==
	 LIBSSH2_ERROR_EAGAIN )
    {
      waitsocket(sock, session);
    }
  if( channel == NULL )
    {
      die("No channel found\n");
    }

// TODO: Add parameters to the server?
// TODO: Fix color configuration
  const char *commandline = "mosh-server new";

  while( (rc = libssh2_channel_exec(channel, commandline)) ==
	 LIBSSH2_ERROR_EAGAIN )
    {
      waitsocket(sock, session);
    }
  if( rc != 0 )
    {
      die("Error executing command\n");
    } 

  string result;
  for( ;; )
    {
      /* loop until we block */
      int rc;
      do
	{
	  char buffer[0x4000];
	  rc = libssh2_channel_read( channel, buffer, sizeof(buffer) );

	  if( rc > 0 )
	    {
	      result.append(buffer);
	    }
	  else {
	    if( rc != LIBSSH2_ERROR_EAGAIN )
	      /* no need to output this for the EAGAIN case */
	      fprintf(stderr, "libssh2_channel_read returned %d\n", rc);
	  }
	}
      while( rc > 0 );

      /* this is due to blocking that would occur otherwise so we loop on
	 this condition */
      if( rc == LIBSSH2_ERROR_EAGAIN )
	{
	  waitsocket(sock, session);
	}
      else
	break;
    }
  // Close channel
  while( (rc = libssh2_channel_close(channel)) == LIBSSH2_ERROR_EAGAIN )
    waitsocket(sock, session);

  libssh2_channel_free(channel);
  channel = NULL;
  
  // Shutdown session
  libssh2_session_disconnect(session, "Normal Shutdown, Thank you for playing");
  libssh2_session_free(session);

  close(sock);
  libssh2_exit();

// TODO: It would be great to use the ssh conn and read in parallel, to save
// any race conditions or other conditions.
  printf("%s", result.c_str());
  string ip(host), port, key;
  string line;
  std::istringstream response(result);
  while (std::getline(response, line)) {
    // Not needed as we use a real IP address.
    // if ( line.compare( 0, 8, "MOSH IP " ) == 0 ) { // MOSH IP 127.0.0.1
    //   size_t ip_end = line.find_last_not_of( " \t\n\r" );
    //   if ( ip_end != string::npos && ip_end >= 8 ) {
    //     ip = line.substr( 8, ip_end + 1 - 8 );
    //   }
    // } else 
      if ( line.compare( 0, 13, "MOSH CONNECT " ) == 0 ) { // MOSH CONNECT 60002 0P/WOaijf73SUDP40RXG0A\r
      size_t port_end = line.find_first_not_of( "0123456789", 13 );
      if ( port_end != string::npos && port_end >= 13 ) {
        port = line.substr( 13, port_end - 13 );
      }
      string rest = line.substr( port_end + 1 );
      size_t key_end = rest.find_last_not_of( " \t\n\r" );
      size_t key_valid_end = rest.find_last_of( key_valid_char_set );
      if ( key_valid_end == key_end && key_end + 1 == 22 ) {
        key = rest.substr( 0, key_end + 1 );
      }
      break;
    } else {
      printf( "%s\n", line.c_str() );
    }
  }
  printf( "Connecting to %s, with key %s\n", ip.c_str(), key.c_str());

// TODO: Make sure we have an IP address 
  if ( !ip.size() ) {
    die( "%s: Did not find remote IP address (is SSH ProxyCommand disabled?).",
         argv[0] );
  }

  if ( !key.size() || !port.size() ) {
    die( "%s: Did not find mosh server startup message.", argv[0] );
  }
  
  printf("Good to go! Starting Mosh\n");

  /* Adopt native locale */
  set_native_locale();

  bool success = false;
  try {
    STMClient client(stdin, stdout, 
			  ip.c_str(), port.c_str(), 
			  strdup(key.c_str()), predict.c_str() );
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

  printf( "\n[mosh is exiting.]\n" );

  //free( key );

  return !success;
}
