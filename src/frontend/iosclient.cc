/*
    Mosh: the mobile shell
    Copyright 2012 Keith Winstein

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations including
    the two.

    You must obey the GNU General Public License in all respects for all
    of the code used other than OpenSSL. If you modify file(s) with this
    exception, you may extend this exception to your version of the
    file(s), but you are not obligated to do so. If you do not wish to do
    so, delete this exception statement from your version. If you delete
    this exception statement from all source files in the program, then
    also delete it here.
*/

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

#if HAVE_PTY_H
#include <pty.h>
#elif HAVE_UTIL_H
#include <util.h>
#endif

#include "iosclient.h"
#include "swrite.h"
#include "completeterminal.h"
#include "user.h"
#include "fatal_assert.h"
#include "locale_utils.h"
#include "pty_compat.h"
#include "select.h"
#include "timestamp.h"

#include "networktransport-impl.h"

#include "restoration.pb.h"
#include "crypto.h"

#define STDIN_FILENO in_fd
#define STDOUT_FILENO out_fd

using std::wstring;

void iOSClient::resume( void )
{
  /* Restore termios state */
  // if ( tcsetattr( in_fd, TCSANOW, &raw_termios ) < 0 ) {
  //     perror( "tcsetattr" );
  //     exit( 1 );
  // }

  // /* Put terminal in application-cursor-key mode */
  // swrite( out_fd, display.open().c_str() );

  // /* Flag that outer terminal state is unknown */
  // repaint_requested = true;
}

void iOSClient::init( void )
{
  if ( !is_utf8_locale() ) {
    LocaleVar native_ctype = get_ctype();
    string native_charset( locale_charset() );

    fprintf( out_fd, "mosh-client needs a UTF-8 native locale to run.\r\n" );
    fprintf( out_fd, "Unfortunately, the client's environment (%s) specifies\nthe character set \"%s\".\r\n", native_ctype.str().c_str(), native_charset.c_str() );
    //int unused __attribute((unused)) = system( "locale" );
    exit( 1 );
  }

//   /* Verify terminal configuration */
//   if ( tcgetattr( in_fd, &saved_termios ) < 0 ) {
//     perror( "tcgetattr" );
//     exit( 1 );
//   }

//   /* Put terminal driver in raw mode */
//   raw_termios = saved_termios;

// #ifdef HAVE_IUTF8
//   if ( !(raw_termios.c_iflag & IUTF8) ) {
//     //    fprintf( stderr, "Warning: Locale is UTF-8 but termios IUTF8 flag not set. Setting IUTF8 flag.\n" );
//     /* Probably not really necessary since we are putting terminal driver into raw mode anyway. */
//     raw_termios.c_iflag |= IUTF8;
//   }
// #endif /* HAVE_IUTF8 */

//   cfmakeraw( &raw_termios );

//   if ( tcsetattr( in_fd, TCSANOW, &raw_termios ) < 0 ) {
//       perror( "tcsetattr" );
//       exit( 1 );
//   }

//   /* Put terminal in application-cursor-key mode */
//   swrite( out_fd, display.open().c_str() );
  fprintf( out_fd, "%s", display.open().c_str() );

  // TODO: Send signal to set window Title
  // /* Add our name to window title */
  // if ( !getenv( "MOSH_TITLE_NOPREFIX" ) ) {
  //   overlays.set_title_prefix( wstring( L"[mosh] " ) );
  // }

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
    wstring escape_pass_name = std::wstring(tmp.begin(), tmp.end());
    tmp = string( escape_key_name_buf );
    wstring escape_key_name = std::wstring(tmp.begin(), tmp.end());
    escape_key_help = L"Commands: Ctrl-Z suspends, \".\" quits, " + escape_pass_name + L" gives literal " + escape_key_name;
    overlays.get_notification_engine().set_escape_key_string( tmp );
  }
  wchar_t tmp[ 128 ];
  swprintf( tmp, 128, L"Nothing received from server on UDP port %s.", port.c_str() );
  connecting_notification = wstring( tmp );
}

void iOSClient::shutdown( void )
{
  /* Restore screen state */
  overlays.get_notification_engine().set_notification_string( wstring( L"" ) );
  overlays.get_notification_engine().server_heard( timestamp() );
  overlays.set_title_prefix( wstring( L"" ) );
  output_new_frame();

  /* Restore terminal and terminal-driver state */
  //swrite( out_fd, display.close().c_str() );
  fprintf( out_fd, "%s", display.close().c_str() );
  
  // if ( tcsetattr( in_fd, TCSANOW, &saved_termios ) < 0 ) {
  //   perror( "tcsetattr" );
  //   exit( 1 );
  // }

  if ( still_connecting() ) {
    fprintf( out_fd, "\nmosh did not make a successful connection to %s:%s.\r\n", ip.c_str(), port.c_str() );
    fprintf( out_fd, "Please verify that UDP port %s is not firewalled and can reach the server.\r\n\n", port.c_str() );
    fprintf( out_fd, "(By default, mosh uses a UDP port between 60000 and 61000. The -p option\nselects a specific UDP port number.)\r\n" );
  } else if ( network ) {
    if ( !clean_shutdown ) {
      fprintf( out_fd, "\r\n\nmosh did not shut down cleanly. Please note that the\r\nmosh-server process may still be running on the server.\r\n" );
    }
  }
}

void iOSClient::main_init( const string encoded_state )
{

  /* local state */
  local_framebuffer = Terminal::Framebuffer( window_size->ws_col, window_size->ws_row );
  new_state = Terminal::Framebuffer( 1, 1 );

  /* initialize screen */
  string init = display.new_frame( false, local_framebuffer, local_framebuffer );
  fwrite( init.data(), init.size(), 1, out_fd );

  /* open network */
  Network::UserStream blank;
  Terminal::Complete local_terminal( window_size->ws_col, window_size->ws_row );

  Restoration::Context context;
  uint64_t now = timestamp();

  if (encoded_state.size() > 0 && context.ParseFromString(encoded_state)) {
    Crypto::set_seq(context.seq());
    blank.apply_string(context.current_state_patch());

    list < TimestampedState<Terminal::Complete> > received_states;
    int received_count = context.received_states_size();

   for (int i = 0; i < received_count; i++) {
     Restoration::TimestampedState ts = context.received_states(i);
     Terminal::Complete state( window_size->ws_col, window_size->ws_row );
     state.apply_string(ts.patch());
     local_terminal = state;
     uint64_t restored_ts = 0;
     if (ts.timestamp() < now) {
       restored_ts = now - ts.timestamp();
     }
     received_states.push_back(TimestampedState<Terminal::Complete>(restored_ts, ts.num(), state));
   }

   list < TimestampedState<Network::UserStream> > sent_states;
   int sent_count = context.sent_states_size();
   for (int i = 0; i < sent_count; i++) {
     Restoration::TimestampedState ts = context.sent_states(i);
     Network::UserStream state;
     state.apply_string(ts.patch());
     uint64_t restored_ts = 0;
     if (ts.timestamp() < now) {
       restored_ts = now - ts.timestamp();
     }
     sent_states.push_back(TimestampedState<Network::UserStream>(restored_ts, ts.num(), state));
   }

   network = new Network::Transport< Network::UserStream, Terminal::Complete >( blank, local_terminal, key.c_str(), ip.c_str(), port.c_str(), sent_states, received_states);
  } else {
    network = new Network::Transport< Network::UserStream, Terminal::Complete >( blank, local_terminal,
									       key.c_str(), ip.c_str(), port.c_str() );
  }

  network->set_send_delay( 1 ); /* minimal delay on outgoing keystrokes */

  /* tell server the size of the terminal */
  network->get_current_state().push_back( Parser::Resize( window_size->ws_col, window_size->ws_row ) );
}

void iOSClient::output_new_frame( void )
{
  if ( !network ) { /* clean shutdown even when not initialized */
    return;
  }

  /* fetch target state */
  new_state = network->get_latest_remote_state().state.get_fb();

  /* apply local overlays */
  overlays.apply( new_state );

  /* calculate minimal difference from where we are */
  const string diff( display.new_frame( !repaint_requested,
					local_framebuffer,
					new_state ) );
  //swrite( STDOUT_FILENO, diff.data(), diff.size() );
  fwrite( diff.data(), diff.size(), 1, out_fd);

  repaint_requested = false;

  local_framebuffer = new_state;
}

void iOSClient::process_network_input( void )
{
  network->recv();
  
  /* Now give hints to the overlays */
  overlays.get_notification_engine().server_heard( network->get_latest_remote_state().timestamp );
  overlays.get_notification_engine().server_acked( network->get_sent_state_acked_timestamp() );

  overlays.get_prediction_engine().set_local_frame_acked( network->get_sent_state_acked() );
  overlays.get_prediction_engine().set_send_interval( network->send_interval() );
  overlays.get_prediction_engine().set_local_frame_late_acked( network->get_latest_remote_state().state.get_echo_ack() );
}

int ret1 = 0;

bool iOSClient::process_user_input( int fd )
{
  const int buf_size = 16384;
  char buf[ buf_size ];

  /* fill buffer if possible */
  ssize_t bytes_read = read( fd, buf, buf_size );
  if ( bytes_read == 0 ) { /* EOF */
    return false;
  } else if ( bytes_read < 0 ) {
    perror( "read" );
    return false;
  }

  if (!network->shutdown_in_progress() ) {
    overlays.get_prediction_engine().set_local_frame_sent( network->get_sent_state_last() );

    for ( int i = 0; i < bytes_read; i++ ) {
      char the_byte = buf[ i ];

      overlays.get_prediction_engine().new_user_byte( the_byte, local_framebuffer );

      if ( quit_sequence_started ) {
	if ( the_byte == '.' ) { /* Quit sequence is Ctrl-^ . */
	  if ( network->has_remote_addr() && (!network->shutdown_in_progress()) ) {
	    overlays.get_notification_engine().set_notification_string( wstring( L"Exiting on user request..." ), true );
	    network->start_shutdown();
	    return true;
	  } else {
	    return false;
	  }
	} else if ( the_byte == 0x0C ) { // Ctrl-L
	  repaint_requested = true;
	} else if ( the_byte == 0x1a ) { /* Suspend sequence is escape_key Ctrl-Z */

    Restoration::Context states;
    uint64_t now = timestamp();
    list < TimestampedState<Terminal::Complete> > received_states = network->get_received_states();
    list < TimestampedState<Network::UserStream> > sent_states = network->get_sent_states();

    Network::UserStream blank;
    string current_state_patch = network->get_current_state().diff_from(blank);
    states.set_current_state_patch(current_state_patch);

    network->start_shutdown();
    states.set_seq(Crypto::seq());

    for ( list< TimestampedState<Terminal::Complete> >::iterator i = received_states.begin();
        i != received_states.end();
        i++ ) {
      Restoration::TimestampedState * ts = states.add_received_states();

      TimestampedState<Terminal::Complete> state = *i;
      uint64_t ts_offset = now - state.timestamp;
      ts->set_timestamp(ts_offset);
      ts->set_num(state.num);
      ts->set_patch(state.state.init_diff());
    }

    for ( list< TimestampedState<Network::UserStream> >::iterator i = sent_states.begin();
        i != sent_states.end();
        i++ ) {
      Restoration::TimestampedState * ts = states.add_sent_states();

      TimestampedState<Network::UserStream> state = *i;
      uint64_t ts_offset = now - state.timestamp;
      ts->set_timestamp(ts_offset);
      ts->set_num(state.num);
      ts->set_patch(state.state.init_diff());
    }

    string encodedState = states.SerializeAsString();
    size_t encodedStateSize = encodedState.size();
    state_callback(state_callback_context, encodedState.data(), encodedStateSize);

    pthread_exit(&ret1);


    //throw std::logic_error("suspending");

    /* Restore terminal and terminal-driver state */
	  // swrite( out_fd, display.close().c_str() );

	  // if ( tcsetattr( in_fd, TCSANOW, &saved_termios ) < 0 ) {
	  //   perror( "tcsetattr" );
	  //   exit( 1 );
	  // }

	  // printf( "\n\033[37;44m[mosh is suspended.]\033[m\n" );

	  // fflush( NULL );

	  // /* actually suspend */
	  // kill( 0, SIGSTOP );

	  // resume();
	} else if ( (the_byte == escape_pass_key) || (the_byte == escape_pass_key2) ) {
	  /* Emulation sequence to type escape_key is escape_key +
	     escape_pass_key (that is escape key without Ctrl) */
	  network->get_current_state().push_back( Parser::UserByte( escape_key ) );
	} else {
	  /* Escape key followed by anything other than . and ^ gets sent literally */
	  network->get_current_state().push_back( Parser::UserByte( escape_key ) );
	  network->get_current_state().push_back( Parser::UserByte( the_byte ) );	  
	}

	quit_sequence_started = false;

	if ( overlays.get_notification_engine().get_notification_string() == escape_key_help ) {
	  overlays.get_notification_engine().set_notification_string( L"" );
	}

	continue;
      }

      quit_sequence_started = (escape_key > 0) && (the_byte == escape_key) && (lf_entered || (! escape_requires_lf));
      if ( quit_sequence_started ) {
	lf_entered = false;
	overlays.get_notification_engine().set_notification_string( escape_key_help, true, false );
	continue;
      }

      lf_entered = ( (the_byte == 0x0A) || (the_byte == 0x0D) ); /* LineFeed, Ctrl-J, '\n' or CarriageReturn, Ctrl-M, '\r' */

      /* We replacing it with Ctrl-^ Ctrl-L
      if ( the_byte == 0x0C ) { // Ctrl-L
	repaint_requested = true;
      }
      */

      network->get_current_state().push_back( Parser::UserByte( the_byte ) );		
    }
  }

  return true;
}

bool iOSClient::process_resize( void )
{
  /* get new size */
  // if ( ioctl( in_fd, TIOCGWINSZ, &window_size ) < 0 ) {
  //   perror( "ioctl TIOCGWINSZ" );
  //   return false;
  // }
  
  // /* tell remote emulator */
  Parser::Resize res( window_size->ws_col, window_size->ws_row );
  
  if ( !network->shutdown_in_progress() ) {
    network->get_current_state().push_back( res );
  }

  /* note remote emulator will probably reply with its own Resize to adjust our state */
  
  /* tell prediction engine */
  overlays.get_prediction_engine().reset();

  return true;
}

bool iOSClient::main( const string encoded_state )
{
  /* initialize signal handling and structures */
  Select &sel = Select::get_instance();
  sel.add_signal( SIGWINCH );
  sel.add_signal( SIGTERM );
  sel.add_signal( SIGINT );
  sel.add_signal( SIGHUP );
  sel.add_signal( SIGPIPE );
  sel.add_signal( SIGCONT );
  sel.add_signal( SIGINFO );
  main_init(encoded_state);

  /* Drop unnecessary privileges */
#ifdef HAVE_PLEDGE
  /* OpenBSD pledge() syscall */
  if ( pledge( "stdio inet tty", NULL )) {
    perror( "pledge() failed" );
    exit( 1 );
  }
#endif

  /* prepare to poll for events */
  //Select &sel = Select::get_instance();

  while ( 1 ) {
    try {
      output_new_frame();

      int wait_time = std::min( network->wait_time(), overlays.wait_time() );

      /* Handle startup "Connecting..." message */
      if ( still_connecting() ) {
	wait_time = std::min( 250, wait_time );
      }

      /* poll for events */
      /* network->fd() can in theory change over time */
      sel.clear_fds();
      std::vector< int > fd_list( network->fds() );
      for ( std::vector< int >::const_iterator it = fd_list.begin();
	    it != fd_list.end();
	    it++ ) {
	sel.add_fd( *it );
      }
      sel.add_fd( STDIN_FILENO );

      int active_fds = sel.select( wait_time );
      if ( active_fds < 0 ) {
	perror( "select" );
	break;
      }

      bool network_ready_to_read = false;

      for ( std::vector< int >::const_iterator it = fd_list.begin();
	    it != fd_list.end();
	    it++ ) {
	if ( sel.read( *it ) ) {
	  /* packet received from the network */
	  /* we only read one socket each run */
	  network_ready_to_read = true;
	}
      }

      if ( network_ready_to_read ) {
	process_network_input();
      }
    
      if ( sel.read( STDIN_FILENO ) ) {
	/* input from the user needs to be fed to the network */
	if ( !process_user_input( STDIN_FILENO ) ) {
	  if ( !network->has_remote_addr() ) {
	    break;
	  } else if ( !network->shutdown_in_progress() ) {
	    overlays.get_notification_engine().set_notification_string( wstring( L"Exiting..." ), true );
	    network->start_shutdown();
	  }
	}
      }

      if ( sel.signal( SIGWINCH ) ) {
        /* resize */
        if ( !process_resize() ) { return false; }
      }

      if ( sel.signal( SIGCONT ) ) {
	resume();
      }

      if ( sel.signal( SIGTERM )
           || sel.signal( SIGINT )
           || sel.signal( SIGHUP )
           || sel.signal( SIGPIPE ) ) {
        /* shutdown signal */
        if ( !network->has_remote_addr() ) {
          break;
        } else if ( !network->shutdown_in_progress() ) {
          overlays.get_notification_engine().set_notification_string( wstring( L"Signal received, shutting down..." ), true );
            network->start_shutdown();
        }
      }

      if ( sel.signal( SIGINFO ) ) {
        uint64_t now = timestamp();

        Restoration::Context states;
        list < TimestampedState<Terminal::Complete> > received_states = network->get_received_states();
        list < TimestampedState<Network::UserStream> > sent_states = network->get_sent_states();

        Network::UserStream blank;
        string current_state_patch = network->get_current_state().diff_from(blank);
        states.set_current_state_patch(current_state_patch);

        network->start_shutdown();
        states.set_seq(Crypto::seq());

        for ( list< TimestampedState<Terminal::Complete> >::iterator i = received_states.begin();
            i != received_states.end();
            i++ ) {
          Restoration::TimestampedState * ts = states.add_received_states();

          TimestampedState<Terminal::Complete> state = *i;
          uint64_t ts_offset = now - state.timestamp;
          ts->set_timestamp(ts_offset);
          ts->set_num(state.num);
          ts->set_patch(state.state.init_diff());
        }

        for ( list< TimestampedState<Network::UserStream> >::iterator i = sent_states.begin();
            i != sent_states.end();
            i++ ) {
          Restoration::TimestampedState * ts = states.add_sent_states();

          TimestampedState<Network::UserStream> state = *i;
          uint64_t ts_offset = now - state.timestamp;
          ts->set_timestamp(ts_offset);
          ts->set_num(state.num);
        ts->set_patch(state.state.init_diff());
      }

      string encodedState = states.SerializeAsString();
      size_t encodedStateSize = encodedState.size();
      state_callback(state_callback_context, encodedState.data(), encodedStateSize);

      clean_shutdown = true;
      break;
          }


      /* quit if our shutdown has been acknowledged */
      if ( network->shutdown_in_progress() && network->shutdown_acknowledged() ) {
	clean_shutdown = true;
	break;
      }

      /* quit after shutdown acknowledgement timeout */
      if ( network->shutdown_in_progress() && network->shutdown_ack_timed_out() ) {
	break;
      }

      /* quit if we received and acknowledged a shutdown request */
      if ( network->counterparty_shutdown_ack_sent() ) {
	clean_shutdown = true;
	break;
      }

      /* write diagnostic message if can't reach server */
      if ( still_connecting()
	   && (!network->shutdown_in_progress())
	   && (timestamp() - network->get_latest_remote_state().timestamp > 250) ) {
	if ( timestamp() - network->get_latest_remote_state().timestamp > 15000 ) {
	  if ( !network->shutdown_in_progress() ) {
	    overlays.get_notification_engine().set_notification_string( wstring( L"Timed out waiting for server..." ), true );
	    network->start_shutdown();
	  }
	} else {
	  overlays.get_notification_engine().set_notification_string( connecting_notification );
	}
      } else if ( (network->get_remote_state_num() != 0)
		  && (overlays.get_notification_engine().get_notification_string()
		      == connecting_notification) ) {
	overlays.get_notification_engine().set_notification_string( L"" );
      }

      network->tick();

      string & send_error = network->get_send_error();
      if ( !send_error.empty() ) {
        overlays.get_notification_engine().set_network_error( send_error );
	send_error.clear();
      } else {
        overlays.get_notification_engine().clear_network_error();
      }
    } catch ( const Network::NetworkException &e ) {
      if ( !network->shutdown_in_progress() ) {
        overlays.get_notification_engine().set_network_error( e.what() );
      }

      struct timespec req;
      req.tv_sec = 0;
      req.tv_nsec = 200000000; /* 0.2 sec */
      nanosleep( &req, NULL );
      freeze_timestamp();
    } catch ( const Crypto::CryptoException &e ) {
      if ( e.fatal ) {
        throw;
      } else {
        wchar_t tmp[ 128 ];
        swprintf( tmp, 128, L"Crypto exception: %s", e.what() );
        overlays.get_notification_engine().set_notification_string( wstring( tmp ) );
      }
    }
  }
  return clean_shutdown;
}
