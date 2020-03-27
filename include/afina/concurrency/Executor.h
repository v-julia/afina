#ifndef AFINA_CONCURRENCY_EXECUTOR_H
#define AFINA_CONCURRENCY_EXECUTOR_H


#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>



namespace Afina {
namespace Concurrency {

/**
* # Thread pool
*/

//-- ДЗ_№3
class Executor;
void perform(Executor *executor);
//-------

class Executor {
    //-- ДЗ_№3: добавлено public, потому, что не получится создать объект класса и вызвать Stop
public:
    //-------
    enum class State {
        // Threadpool is fully operational, tasks could be added and get executed
        kRun,

        // Threadpool is on the way to be shutdown, no ned task could be added, but existing will be
        // completed as requested
        kStopping,

        // Threadppol is stopped
        kStopped
    };
    
    //-- ДЗ_№3: 
    // здесь надо добавить параметры пула потоков, непонятно, зачем name и size
    // Executor(std::string name, int size);
    // на всякий случай комментируем и заменяем на такой
    Executor( std::size_t lw, std::size_t hw, std::size_t mqs, std::size_t it ):
    state(State::kRun),
    low_watermark(lw),
    hight_watermark(hw),
    max_queue_size(mqs),
    idle_time(it),
    threads_waiting_count(lw),
    threads_running_count(0)
    {
        //  минимальное количество потоков можно создать сразу, пока очередь пустая
        for (size_t i = 0; i < low_watermark; i++) {
            std::thread t(&(perform), this);
            t.detach();
        }
    };

    ~Executor(){Stop(true);};// пока что ничего не надо делать, само уничтожится
    //-------

    /**
    * Signal thread pool to stop, it will stop accepting new jobs and close threads just after each become
    * free. All enqueued jobs will be complete.
    *
    * In case if await flag is true, call won't return until all background jobs are done and all threads are stopped
    */
    void Stop(bool await = false);

    /**
    * Add function to be executed on the threadpool. Method returns true in case if task has been placed
    * onto execution queue, i.e scheduled for execution and false otherwise.
    *
    * That function doesn't wait for function result. Function could always be written in a way to notify caller about
    * execution finished by itself
    */
    template <typename F, typename... Types> 
    bool Execute(F &&func, Types... args) 
    {
        // Prepare "task"
        auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);

        std::unique_lock<std::mutex> lock(this->mutex);
        if (state != State::kRun) {
            return false;
        }

        //-- ДЗ_№3
        // проверить tasks на max_queue_size, если не помещается, то вернуть false
        if (tasks.size() > max_queue_size) {
            return false;
        }
        //-------


        // Enqueue new task
        tasks.push_back(exec);
        
        // если окажется, что нужны еще дополнительные потоки, то надо создать еще один в рамках hight_watermark
        std::size_t threads_count=threads_running_count+threads_waiting_count;
        if (threads_count < hight_watermark && threads_waiting_count == 0) {
            ++threads_waiting_count;
            std::thread t(&perform, this);
            t.detach();
        }

        
        //похоже, что освободившийся поток сам найдет задачу в очереди
        empty_condition.notify_one();
        return true;
    }

private:
    // No copy/move/assign allowed
    Executor(const Executor &);            // = delete;
    Executor(Executor &&);                 // = delete;
    Executor &operator=(const Executor &); // = delete;
    Executor &operator=(Executor &&);      // = delete;

    /**
    * Main function that all pool threads are running. It polls internal task queue and execute tasks
    */
    friend void perform(Executor *executor);

    /**
    * Mutex to protect state below from concurrent modification
    */
    std::mutex mutex;

    /**
    * Conditional variable to await new data in case of empty queue
    */
    std::condition_variable empty_condition;

    /**
    * Vector of actual threads that perorm execution
    */
    std::vector<std::thread> threads;
    /**
    * так как в ДЗ_№3 требуется удалять свободные потоки и создавать новые в рамках low и high
    * складывать их в вектор наверное не стоит, потому что сложно будет их перебирать
    */
     

    /**
    * Task queue
    */
    std::deque<std::function<void()>> tasks;

    /**
    * Flag to stop bg threads
    */
    State state;
    std::condition_variable stop_condition; 
    
    //-- ДЗ_№3
    std::size_t low_watermark;   // минимальное количество потоков, которое должно быть в пуле.
    std::size_t hight_watermark; // максимальное количество потоков в пуле
    std::size_t max_queue_size;  // максимальное число задач в очереди
    std::size_t idle_time; // количество миллисекунд, которое каждый из потоков ждет задач. Если поток не получает задачу в течении этого времени он должен быть убит и удален из пула, если только это не приводит к нарушению low_watermark инварианта.
    std::size_t threads_waiting_count=0;
    std::size_t threads_running_count=0;
    
     //-------

    
    
};

} // namespace Concurrency
} // namespace Afina

#endif // AFINA_CONCURRENCY_EXECUTOR_H
