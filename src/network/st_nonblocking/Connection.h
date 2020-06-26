#ifndef AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H

#include <string>

#include <sys/epoll.h>


    // добавлено для ДЗ №4,5
    // похоже, здесь надо как-то разместить весь код, что был раньше в st_blocking?
    // относящийся к обработке команд и выполнению команд (storage,execute, парсер и др.)
    // и при этом действия, которые делаются в OnRun здесь разделяются на куски: 
    // чтение, запись частями по мере доступности соединения
#include <memory>
#include <vector>
#include <cassert>
#include <spdlog/logger.h>
#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>
#include <afina/execute/Command.h>
#include <protocol/Parser.h>
    // ----------------------



namespace Afina {
namespace Network {
namespace STnonblock {

    
    
    
    
class Connection {
public:
    // изменение для ДЗ №4 - 'std::shared_ptr<Afina::Storage>', 'std::shared_ptr<spdlog::logger>'
    Connection(int s, std::shared_ptr<Afina::Storage> ps, std::shared_ptr<spdlog::logger> pl) :
    _socket(s), 
    pstorage(ps), 
    plogger(pl), 
    is_alive(false) 
    {
        std::memset(&_event, 0, sizeof(struct epoll_event));
        _event.data.ptr = this;
        plogger->info("Created st_nonblocking Connection");
    }

    inline bool isAlive() const { return is_alive; }

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;
    
    // добавлено для ДЗ №4
    std::shared_ptr<Afina::Storage> pstorage;
    std::shared_ptr<spdlog::logger> plogger;
    bool is_alive;
    
    // это надо сделать здесь, потому, что они инициализируются в момент создания
    // подключения, в процессе работы должны сохранять значения, а удаляются при разрыве соединения
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
                       
                    
    int readed_bytes;
    char client_buffer[4096];
    int written_bytes;
    std::vector<std::string> results;
    
    // ----------------------
};

} // namespace STnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
