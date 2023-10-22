#include <iostream>
#include <memory>
#include <uvw.hpp>

void echo_server(const uvw::listen_event&, uvw::tcp_handle& srv)
{
    std::shared_ptr<uvw::tcp_handle> client = srv.parent().resource<uvw::tcp_handle>();

    client->on<uvw::end_event>([](const uvw::end_event&,
                                   uvw::tcp_handle& client) { client.close(); });
    client->on<uvw::data_event>(
        [](uvw::data_event& data, uvw::tcp_handle& client) {
            client.write(std::move(data.data), data.length);
        });

    srv.accept(*client);
    client->read();
}
void listen(uvw::loop& loop)
{
    std::shared_ptr<uvw::tcp_handle> tcp = loop.resource<uvw::tcp_handle>();

    tcp->on<uvw::listen_event>(echo_server);

    tcp->bind("127.0.0.1", 3490);
    tcp->listen();
}

int main()
{
    auto loop = uvw::loop::get_default();
    listen(*loop);
    loop->run();
}
