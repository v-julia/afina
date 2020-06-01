#include "Connection.h"

#include <iostream>
#include <afina/network/Server.h>
#include <sys/uio.h>

namespace Afina {
namespace Network {
namespace STnonblock {

// See Connection.h
void Connection::Start()
{
    plogger->info("Start new Connection: client_socket={}\n", _socket);
    readed_bytes = 0;
    arg_remains = 0;
    _event.events = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
    argument_for_command.resize(0);
    command_to_execute.reset();
    parser.Reset();
    is_alive = true;
    written_bytes = 0;
    results.clear();
    _event.data.ptr = this;
}

// See Connection.h
void Connection::OnError()
{
    plogger->error("Error in Connection: client_socket={}\n\n", _socket);
    OnClose();
}

// See Connection.h
void Connection::OnClose()
{
    close(_socket);
    plogger->info("Close Connection: client_socket={}\n", _socket);
    is_alive = false;
}

// See Connection.h
void Connection::DoRead()
{
// здесь c небольшими изменениями копируем код из st_blocking функции OnRun (с учетом исправления в 171 строке)
// отличие в том, что здесь не надо сразу же делать ответ сокету, а только прочитать и выполнить команду
// в этом коде был logger, который тоже надо подключить по аналогии с ServerImpl

// Process new connection:
// - read commands until socket alive
// - execute each command
// - send response
    try {
        while ( ( readed_bytes += read(_socket, client_buffer+readed_bytes, sizeof(client_buffer) - readed_bytes) ) > 0 ) {
            plogger->debug("Got {} bytes from socket", readed_bytes);

            // Single block of data readed from the socket could trigger inside actions a multiple times,
            // for example:
            // - read#0: [<command1 start>]
            // - read#1: [<command1 end> <argument> <command2> <argument for command 2> <command3> ... ]
            while ( readed_bytes > 0 ) {
                plogger->debug("Process {} bytes", readed_bytes);
                // There is no command yet
                if ( !command_to_execute ) {
                    std::size_t parsed = 0;
                    if ( parser.Parse(client_buffer, readed_bytes, parsed) ) {
                        // There is no command to be launched, continue to parse input stream
                        // Here we are, current chunk finished some command, process it
                        plogger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
                        command_to_execute = parser.Build(arg_remains);
                        if ( arg_remains > 0 ) {
                            arg_remains += 2;
                        }
                    }

                    // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                    // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                    if ( parsed == 0 ) {
                        break;
                    }
                    else {
                        std::memmove(client_buffer, client_buffer + parsed, readed_bytes - parsed);
                        readed_bytes -= parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if ( command_to_execute && arg_remains > 0 ) {
                    plogger->debug("Fill argument: {} bytes of {}", readed_bytes, arg_remains);
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(arg_remains, std::size_t(readed_bytes));
                    argument_for_command.append(client_buffer, to_read);

                    std::memmove(client_buffer, client_buffer + to_read, readed_bytes - to_read);
                    arg_remains -= to_read;
                    readed_bytes -= to_read;
                }

                // Thre is command & argument - RUN!
                if ( command_to_execute && arg_remains == 0 ) {
                    plogger->debug("Start command execution");

                    std::string result;
                    command_to_execute->Execute(*pstorage, argument_for_command, result);
                    bool is_epmty=results.empty();
                    result += "\r\n";
                    results.push_back(result);
                    
                    // если готов результат после выполнения команды, то надо добавить событие записи в сокет
                    if(is_epmty){
                        _event.events |= EPOLLOUT;
                    }
                    // Send response
                    // это будет делать DoWrite
//                     if (send(client_socket, result.data(), result.size(), 0) <= 0) {
//                         throw std::runtime_error("Failed to send response");
//                     }

                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
            } // while (readed_bytes)
        }

        if ( readed_bytes == 0 ) {
            plogger->debug("Connection closed: client_socket={}",_socket);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK){
            throw std::runtime_error(std::string(strerror(errno)));
        }
    }
    catch ( std::runtime_error& ex ) {
        plogger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
        is_alive=false;
    }

    // We are done with this connection
    // в отличие от st_blocking, close не здесь наверное делать
    //close(client_socket);

}




// See Connection.h
void Connection::DoWrite() {
    // здесь действуем по рекомендациям, которые были сделаны на лекции
    assert(!results.empty());
    size_t osize = std::min(results.size(), size_t(64));
    struct iovec vecs[osize];
    try {
        auto result_it = results.begin();
        for(size_t i = 0; i < osize; i++){
          vecs[i].iov_base = &(*result_it)[0];
          vecs[i].iov_len = (*result_it).size();
          result_it++;
        }
        vecs[0].iov_base = (char *)(vecs[0].iov_base) + written_bytes;
        vecs[0].iov_len -= written_bytes;
        int done;
        if ((done = writev(_socket, vecs, results.size())) <= 0) {
            if(errno != EAGAIN && errno != EWOULDBLOCK){
              plogger->error("Failed to send response");
              throw std::runtime_error(std::string(strerror(errno)));
            }
            else{
              return;
            }
        }
        written_bytes += done;
        result_it = results.begin();
        while(result_it != results.end()) {
            if (written_bytes < (*result_it).size()) {
                break;
            }
            written_bytes -= (*result_it).size();
            result_it++;
        }
        results.erase(results.begin(), result_it);
        if (results.empty()) {
            _event.events ^= EPOLLOUT;
        }
    } catch (std::runtime_error &ex) {
        plogger->error("Failed to writing to connection on descriptor {}: {} \n", _socket, ex.what());
        is_alive = false;
    }
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
