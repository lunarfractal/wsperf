/**
 * This example is presently used as a scratch space. It may or may not be broken
 * at any given time.
 */

#include <asio.hpp>

namespace whyamidoingthis = asio;

namespace boost {
    namespace asio = whyamidoingthis;
}

#include <iostream>
#include <fstream>
#include <chrono>
#include <mutex>
#include <type_traits>

struct open_handshake_stats {
    std::chrono::high_resolution_clock::time_point s_start;
    std::chrono::high_resolution_clock::time_point s_tcp_pre_init;
    std::chrono::high_resolution_clock::time_point s_tcp_post_init;
    std::chrono::high_resolution_clock::time_point s_open;
    std::chrono::high_resolution_clock::time_point s_close;

    // some sort of status
    bool s_fail;
};

#include "wspp-config.hpp"

void on_socket_init(websocketpp::connection_hdl hdl, asio::ip::tcp::socket & s) {
    //asio::ip::tcp::no_delay option(true);
    //s.set_option(option);
}


typedef websocketpp::client<websocketpp::config::asio_tls_client> client_tls;
typedef websocketpp::client<websocketpp::config::asio_client> client_no_tls;


template <typename client_type>
class handshake_test {
public:
    typedef handshake_test<client_type> type;
    typedef std::chrono::duration<int,std::micro> dur_type;

    typedef typename client_type::connection_ptr connection_ptr;

    handshake_test () {
        // silence access/error messages
        m_endpoint.set_access_channels(websocketpp::log::alevel::all ^ websocketpp::log::alevel::frame_header);
        m_endpoint.set_error_channels(websocketpp::log::elevel::all);

        // Initialize ASIO
        m_endpoint.init_asio();

        if constexpr (std::is_same_v<client_type, client_tls>) {
            m_endpoint.set_tls_init_handler([](websocketpp::connection_hdl) {
                return websocketpp::lib::make_shared<asio::ssl::context>(asio::ssl::context::tlsv12_client);
            });
        }

        //m_endpoint.set_socket_init_handler(bind(&on_socket_init,::_1,::_2));

        //m_endpoint.set_tcp_pre_init_handler(bind(&type::on_tcp_pre_init,this,::_1));
        //m_endpoint.set_tcp_post_init_handler(bind(&type::on_tcp_post_init,this,::_1));
        m_endpoint.set_open_handler(bind(&type::on_open,this,::_1));
        m_endpoint.set_fail_handler(bind(&type::on_fail,this,::_1));
        m_endpoint.set_close_handler(bind(&type::on_close,this,::_1));
    }

    void start(std::string uri, std::string proxy, size_t num_threads, size_t num_cons, size_t num_parallel_handshakes_low, size_t num_parallel_handshakes_high, std::string logfile) {
        m_stats_list.reserve(num_cons);
        m_uri = uri;
        m_proxy = proxy;
        m_logfile = logfile;
        m_connection_count = num_cons;
        m_max_handshakes_low = num_parallel_handshakes_low;
        m_max_handshakes_high = num_parallel_handshakes_high;
        m_cur_handshakes = 0;
        m_total_connections = 0;
        m_close_immediately = true;
        m_cur_connections = 0;

        m_low_water_mark_count = 0;
        m_high_water_mark_count = 0;

        m_test_start = std::chrono::high_resolution_clock::now();
        m_test_start_wallclock = std::chrono::system_clock::now();

        launch_more_connections();

        if (num_threads > 0) {
            std::vector<std::thread> ts;
            for (size_t i = 0; i < num_threads; i++) {
                ts.push_back(std::thread(&client_type::run, &m_endpoint));
            }
            for (auto & t : ts) {
                t.join();
            }
        } else {
            m_endpoint.run();
        }
    }

    void launch_more_connections() {
        if (m_total_connections < m_connection_count) {
            m_low_water_mark_count++;
        }
        while (m_total_connections < m_connection_count) {
            if (m_cur_handshakes == m_max_handshakes_high) {
                m_high_water_mark_count++;
                break;
            }
            launch_connection(m_uri);
            m_cur_handshakes++;
            m_total_connections++;
        }
    }

    void launch_connection(std::string uri) {
        websocketpp::lib::error_code ec;
        connection_ptr con = m_endpoint.get_connection(uri, ec);

        if (ec) {
        	m_endpoint.get_alog().write(websocketpp::log::alevel::app,ec.message());
        }

        con->set_proxy(m_proxy);

        m_endpoint.connect(con);
	    //con->s_start = std::chrono::high_resolution_clock::now();
    }

    void on_tcp_pre_init(websocketpp::connection_hdl hdl) {
        //connection_ptr con = m_endpoint.get_con_from_hdl(hdl);
        //con->s_tcp_pre_init = std::chrono::high_resolution_clock::now();
    }
    void on_tcp_post_init(websocketpp::connection_hdl hdl) {
        //connection_ptr con = m_endpoint.get_con_from_hdl(hdl);
        //con->s_tcp_post_init = std::chrono::high_resolution_clock::now();
    }

    void on_open(websocketpp::connection_hdl hdl) {
        connection_ptr con = m_endpoint.get_con_from_hdl(hdl);
        //con->s_open = std::chrono::high_resolution_clock::now();
        //con->s_fail = false;

        if (m_close_immediately) {
            con->close(websocketpp::close::status::going_away,"");
        }

        std::lock_guard<std::mutex> guard(m_stats_lock);
        m_cur_handshakes--;
        m_cur_connections++;

        // check if we need to launch more connections
        if (m_cur_handshakes < m_max_handshakes_low) {
            launch_more_connections();
        }

        // check if we need to close any connections
        if (m_total_connections == m_connection_count) {
            // close connections
        }
    }

    void on_fail(websocketpp::connection_hdl hdl) {
        //connection_ptr con = m_endpoint.get_con_from_hdl(hdl);
        //con->s_open = std::chrono::high_resolution_clock::now();
        //con->s_close = con->s_open;
        //con->s_fail = true;

        std::lock_guard<std::mutex> guard(m_stats_lock);
        m_cur_handshakes--;

        // Add stats to the list
        //m_stats_list.push_back(con->stats);

        // check if we need to launch more connections
        if (m_cur_handshakes < m_max_handshakes_low) {
            launch_more_connections();
        }

        // check if we are done
        if (m_stats_list.size() == m_connection_count) {
            test_complete();
        }


    }

    void on_close(websocketpp::connection_hdl hdl) {
        //connection_ptr con = m_endpoint.get_con_from_hdl(hdl);
        //con->s_close = std::chrono::high_resolution_clock::now();

        std::lock_guard<std::mutex> guard(m_stats_lock);
        m_cur_connections--;

        // Add stats to the list
        //m_stats_list.push_back(*websocketpp::lib::static_pointer_cast<open_handshake_stats>(con));

        // Check it we are done
        if (m_stats_list.size() == m_connection_count) {
            test_complete();
        }
    }

    void test_complete() {
        m_test_end = std::chrono::high_resolution_clock::now();
        m_test_end_wallclock = std::chrono::system_clock::now();

        std::ofstream logfile;
        logfile.open(m_logfile);

        logfile << "{\"total_duration\":"
                << std::chrono::duration_cast<dur_type>(m_test_end-m_test_start).count()
                << ",\"started\":" << m_test_start_wallclock.time_since_epoch().count()
                << ",\"ended\":" << m_test_end_wallclock.time_since_epoch().count()
                << ",\"handshake_throttle_count\":" << m_high_water_mark_count
                << ",\"handshake_resume_count\":" << m_low_water_mark_count
                << ",\"connection_stats\":[";
        bool first = true;
        for (auto i : m_stats_list) {
            logfile << (!first ? "," : "") << "{\"tcp_pre_init\":"
                    << std::chrono::duration_cast<dur_type>(i.s_tcp_pre_init-i.s_start).count()
                    << ",\"tcp_post_init\":"
                    << std::chrono::duration_cast<dur_type>(i.s_tcp_post_init-i.s_tcp_pre_init).count()
                    << ",\"open\":"
                    << std::chrono::duration_cast<dur_type>(i.s_open-i.s_tcp_post_init).count()
                    << ",\"close\":"
                    << std::chrono::duration_cast<dur_type>(i.s_close-i.s_open).count()
                    << ",\"failed\":" << (i.s_fail ? "true" : "false")
                    << "}";
            first = false;
        }
        logfile << "]}" << std::endl;

        logfile.close();
    }
private:
    client_type m_endpoint;

    std::string m_uri;
    std::string m_proxy;
    std::string m_logfile;
    size_t m_connection_count;
    size_t m_max_handshakes_high;
    size_t m_max_handshakes_low;
    size_t m_cur_handshakes;
    size_t m_cur_connections;
    size_t m_total_connections;

    size_t m_high_water_mark_count;
    size_t m_low_water_mark_count;

    bool m_close_immediately;

    std::chrono::high_resolution_clock::time_point m_test_start;
    std::chrono::high_resolution_clock::time_point m_test_end;

    std::chrono::system_clock::time_point m_test_start_wallclock;
    std::chrono::system_clock::time_point m_test_end_wallclock;

    std::mutex m_stats_lock;
    std::vector<open_handshake_stats> m_stats_list;
};

int main(int argc, char* argv[]) {
    std::string logfile;
    std::string uri;
    std::string proxy;
    size_t num_threads;
    size_t num_cons;
    size_t max_parallel_handshakes_low;
    size_t max_parallel_handshakes_high;

	if (argc == 8) {
	    uri = argv[1];
            proxy = argv[2];
	    num_threads = atoi(argv[3]);
	    num_cons = atoi(argv[4]);
	    max_parallel_handshakes_low = atoi(argv[5]);
	    max_parallel_handshakes_high = atoi(argv[6]);
        logfile = argv[7];
	} else {
	    std::cout << "Usage: wsperf serverurl num_threads num_connections max_parallel_handshakes_low max_parallel_handshakes_high" << std::endl;
	    std::cout << "Example: wsperf ws://localhost:9002 http://localhost:8087 4 50 25 50 result.json" << std::endl;
	    return 1;
	}

	// some input sanity checking
	if (max_parallel_handshakes_low == 0 || max_parallel_handshakes_low > max_parallel_handshakes_high) {
	    std::cout << "max_parallel_handshakes_low must be positive and less than max_parallel_handshakes_high" << std::endl;
	    return 1;
	}

	try {
        if (uri.substr(0,3) == "wss") {
            handshake_test<client_tls> endpoint;
            endpoint.start(uri,proxy,num_threads,num_cons,max_parallel_handshakes_low,max_parallel_handshakes_high,logfile);
            //std::cout << "wss not supported at the moment" << std::endl;
        } else {
            handshake_test<client_no_tls> endpoint;
            endpoint.start(uri,proxy,num_threads,num_cons,max_parallel_handshakes_low,max_parallel_handshakes_high,logfile);
        }
    } catch (const std::exception & e) {
        std::cout << e.what() << std::endl;
    } catch (websocketpp::lib::error_code e) {
        std::cout << e.message() << std::endl;
    } catch (...) {
        std::cout << "other exception" << std::endl;
    }
}
