#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <string>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include "logger.hpp"


namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HTTPServer {
public:
    void run() {
        auto const address = net::ip::make_address("0.0.0.0");
        auto const port = static_cast<unsigned short>(8080);
        
        net::io_context ioc{1};
        tcp::acceptor acceptor{ioc, {address, port}};
        
        std::cout << "Server listening on http://" << address << ":" << port << "\n";
        
        while (true) {
            tcp::socket socket{ioc};
            acceptor.accept(socket);
            
            try {
                beast::flat_buffer buffer;
                http::request<http::string_body> req;
                http::read(socket, buffer, req);
                
                auto const handle_request = [&req]() {
                    http::response<http::string_body> res{http::status::ok, req.version()};
                    res.set(http::field::server, "Boost Beast Server");
                    res.set(http::field::content_type, "text/html");
                    
                    std::string body = "<html><body><h1>Hello from Boost.Beast!</h1>";
                    body += "<p>Method: " + std::string(req.method_string()) + "</p>";
                    body += "<p>Path: " + std::string(req.target()) + "</p>";
                    body += "</body></html>";
                    
                    res.body() = body;
                    res.prepare_payload();
                    return res;
                };
                
                http::response<http::string_body> res = handle_request();
                http::write(socket, res);
                
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << "\n";
            }
        }
    }
};



