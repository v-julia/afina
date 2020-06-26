#include "ServerImpl.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <spdlog/logger.h>

#include <afina/Storage.h>
#include <afina/logging/Service.h>

#include "Connection.h"
#include "Utils.h"

namespace Afina {
namespace Network {
namespace STnonblock {



// See Server.h
void ServerImpl::Start(uint16_t port, uint32_t n_acceptors, uint32_t n_workers) {
    _logger = pLogging->select("network");
    _logger->info("Start st_nonblocking network service");

    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    // Create server socket
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;         // IPv4
    server_addr.sin_port = htons(port);       // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address

    _server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_server_socket == -1) {
        throw std::runtime_error("Failed to open socket: " + std::string(strerror(errno)));
    }

    int opts = 1;
    if (setsockopt(_server_socket, SOL_SOCKET, (SO_KEEPALIVE), &opts, sizeof(opts)) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket setsockopt() failed: " + std::string(strerror(errno)));
    }

    if (bind(_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket bind() failed: " + std::string(strerror(errno)));
    }

    make_socket_non_blocking(_server_socket); // такого небыло раньше
    if (listen(_server_socket, 5) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket listen() failed: " + std::string(strerror(errno)));
    }

    // eventfd - create a file descriptor for event notification
    // creates an "eventfd object" that can be used as an event
    // wait/notify mechanism by user-space applications, and by the kernel
    // to notify user-space applications of events.  The object contains an
    // unsigned 64-bit integer (uint64_t) counter that is maintained by the
    // kernel.  This counter is initialized with the value specified in the
    // argument initval.
    // returns a new file descriptor that can be used to refer to the eventfd object
    _event_fd = eventfd(0, EFD_NONBLOCK); 
    if (_event_fd == -1) {
        throw std::runtime_error("Failed to create epoll file descriptor: " + std::string(strerror(errno)));
    }

    _work_thread = std::thread(&ServerImpl::OnRun, this); // только один этот поток и будет работать
}

// See Server.h
void ServerImpl::Stop() {
    _logger->warn("Stop network service");

    // Wakeup threads that are sleep on epoll_wait
    if (eventfd_write(_event_fd, 1)) {
        throw std::runtime_error("Failed to wakeup workers");
    }

    close(_server_socket);
}

// See Server.h
void ServerImpl::Join() {
    // Wait for work to be complete
    _work_thread.join();
}

// See ServerImpl.h
void ServerImpl::OnRun() {
    _logger->info("Start acceptor");
    // здесь нетак как раньше, нет никаких парсеров и экзекуторов
    // создает объект(instance) 'epoll' и возвращает его дескриптор
    // этот объект предназначается для мониторинга событий 
    int epoll_descr = epoll_create1(0);
    if (epoll_descr == -1) {
        throw std::runtime_error("Failed to create epoll file descriptor: " + std::string(strerror(errno)));
    }

    // для сервера инициализация структуры событий и декскрипторов epoll_event
    // struct epoll_event{uint32_t events;Epoll events    epoll_data_t data;User data variable} __EPOLL_PACKED;
    // typedef union epoll_data {void *ptr; int fd; uint32_t u32; uint64_t u64;} epoll_data_t;
    struct epoll_event event; 
    event.events = EPOLLIN;   // входящие события (следить за ними)
    event.data.fd = _server_socket; // epoll никак не использует это поле, поэтому сюда пишется то, что требуется использовать в программе
    if (epoll_ctl(epoll_descr, EPOLL_CTL_ADD, _server_socket, &event)) { // добавить в список1 _server_socket мониторить входящие события
        throw std::runtime_error("Failed to add file descriptor to epoll");
    }

    struct epoll_event event2; // не ясно зачем второй нужен _event_fd ? (used to wakeup workers)
    event2.events = EPOLLIN;
    event2.data.fd = _event_fd;
    if (epoll_ctl(epoll_descr, EPOLL_CTL_ADD, _event_fd, &event2)) { // добавить в список2 _event_fd
        throw std::runtime_error("Failed to add file descriptor to epoll");
    }

    bool run = true; 
    std::array<struct epoll_event, 64> mod_list; // набор из 64 элементов класса epoll_event
    
    
    
    while (run) {
        int nmod = epoll_wait(epoll_descr, &mod_list[0], mod_list.size(), -1); // ждать появления событий от сокетов (timeout in milliseconds (-1 == infinite))
        _logger->debug("Acceptor wokeup: {} events", nmod);

        for (int i = 0; i < nmod; i++) {
            struct epoll_event &current_event = mod_list[i]; // по очереди перебираются все события
            if (current_event.data.fd == _event_fd) {
                _logger->debug("Break acceptor due to stop signal");
                run = false;
                continue;
            } else if (current_event.data.fd == _server_socket) {
                OnNewConnection(epoll_descr);
                continue;
            }

            // That is some connection!
            Connection *pc = static_cast<Connection *>(current_event.data.ptr);

            auto old_mask = pc->_event.events; // битовая маска events может содержать сведения о нескольких событиях
            if ((current_event.events & EPOLLERR) || (current_event.events & EPOLLHUP)) {
                pc->OnError();
            } else if (current_event.events & EPOLLRDHUP) {
                pc->OnClose();
            } else {
                // Depends on what connection wants...
                if (current_event.events & EPOLLIN) {
                    pc->DoRead();
                }
                if (current_event.events & EPOLLOUT) {
                    pc->DoWrite();
                }
                // все действия должны выполняться в объектах Connection
            }

            // Does it alive?
            if (!pc->isAlive()) {
                if (epoll_ctl(epoll_descr, EPOLL_CTL_DEL, pc->_socket, &pc->_event)) {
                    _logger->error("Failed to delete connection from epoll");
                }

                close(pc->_socket);
                pc->OnClose();

                delete pc;
            } else if (pc->_event.events != old_mask) {
                if (epoll_ctl(epoll_descr, EPOLL_CTL_MOD, pc->_socket, &pc->_event)) {
                    _logger->error("Failed to change connection event mask");

                    close(pc->_socket);
                    pc->OnClose();

                    delete pc;
                }
            }
        }
    }
    _logger->warn("Acceptor stopped");
}

void ServerImpl::OnNewConnection(int epoll_descr) {
    for (;;) {
        struct sockaddr in_addr; // ? не понятно почему это работало раньше в первоначальном коде
        socklen_t in_len;

        // No need to make these sockets non blocking since accept4() takes care of it.
        in_len = sizeof(in_addr);
        int infd = accept4(_server_socket, &in_addr, &in_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (infd == -1) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                break; // We have processed all incoming connections.
            } else {
                _logger->error("Failed to accept socket");
                break;
            }
        }

        // Print host and service info.
        char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
        int retval =
            getnameinfo(&in_addr, in_len, hbuf, sizeof hbuf, sbuf, sizeof sbuf, NI_NUMERICHOST | NI_NUMERICSERV);
        if (retval == 0) {
            _logger->info("Accepted connection on descriptor {} (host={}, port={})\n", infd, hbuf, sbuf);
        }

        // Register the new FD to be monitored by epoll.
        Connection *pc = new(std::nothrow) Connection(infd, pstorage, _logger);
        if (pc == nullptr) {
            throw std::runtime_error("Failed to allocate connection");
        }

        // Register connection in worker's epoll
        pc->Start();
        if (pc->isAlive()) {
            if (epoll_ctl(epoll_descr, EPOLL_CTL_ADD, pc->_socket, &pc->_event)) {
                pc->OnError();
                delete pc;
            }
        }
    }
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
