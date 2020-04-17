#include "Connection.h"

#include <iostream>
#include <afina/network/Server.h>
#include <sys/uio.h>

namespace Afina {
namespace Network {
namespace MTnonblock {

// See Connection.h
void Connection::Start()
{
    plogger->info("Starting new MTnonblock Connection: client_socket={}\n", _socket);
    std::unique_lock<std::mutex> lock(connection_mutex);
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
    std::unique_lock<std::mutex> lock(connection_mutex);
    OnClose();
}

// See Connection.h
void Connection::OnClose()
{
    plogger->info("Close Connection: client_socket={}\n", _socket);
    std::unique_lock<std::mutex> lock(connection_mutex);
    close(_socket);
    is_alive = false;
}

// See Connection.h
void Connection::DoRead()
{
    std::unique_lock<std::mutex> lock(connection_mutex);
    try {
        while ( ( readed_bytes = read(_socket, client_buffer+readed_bytes, sizeof(client_buffer) - readed_bytes) ) > 0 ) {
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
    std::unique_lock<std::mutex> lock(connection_mutex);
    assert(!results.empty());
    size_t sz_results = std::min(results.size(), size_t(64));
    struct iovec out_data[sz_results]; // struct iovec {void *iov_base; /* Starting address */  size_t iov_len;/* Number of bytes */};
    
    try {
        // здесь указать адрес и длину строки из каждого элемента вектора строк results
        for (size_t i = 0; i < sz_results; i++) {
            out_data[i].iov_base = (void *)results[i].c_str(); // results[i].c_str()
            out_data[i].iov_len = results[i].size();
        }
        
        // в самой первой строке надо учесть, что часть байтов, а именно в количестве written_bytes уже записана в предыдущий раз
        out_data[0].iov_base = (char *)(out_data[0].iov_base) + written_bytes;
        out_data[0].iov_len -= written_bytes;
        
        // теперь делается очередная запись и определеяется - сколько байт удалось записать 
        int current_bytes_writen = writev(_socket, out_data, results.size()); //the writev() function returns the number of bytes written
        
        written_bytes += current_bytes_writen; 
        // может оказаться, что удалось записать несколько строк из results
        // поэтому из results надо удалить первые строки, которые записали полностью
        // и оставить в written_bytes количество записанных байт из неполностью записанной строки
        auto it = results.begin();
        while (it != results.end()) {
            if (written_bytes < it->size()) break;
            written_bytes -= it->size();
            it++;
        }
        results.erase(results.begin(), it);
        if (results.empty()) {
            _event.events ^= EPOLLOUT;
            _event.events |= EPOLLIN;
        }
    } catch (std::runtime_error &ex) {
        plogger->error("Failed to writing to connection on descriptor {}: {} \n", _socket, ex.what());
        is_alive = false;
    }
}

} // namespace MTnonblock
} // namespace Network
} // namespace Afina
