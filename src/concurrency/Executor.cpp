#include <afina/concurrency/Executor.h>


namespace Afina {
namespace Concurrency {
    
void perform(Executor *executor) {
    bool stop_thread = false;
    std::function<void()> task;
    while (!stop_thread) 
    {
        // если поток запущен, то он укладывается в допустимое количество потоков
        {
            std::unique_lock<std::mutex> lock(executor->mutex);
            if (executor->state != Executor::State::kRun) 
            {
                // выход из while (!stop_thread) потому что остановлена работа пула
                // и все потоки уничтожаются, а новых не следует создавать
                break;
            }
            // новый период ожидания задачи из очереди, обновить момент времени до которого надо ждать
            auto time = std::chrono::system_clock::now() + std::chrono::milliseconds(executor->idle_time);
            while 
            (
                (executor->tasks.empty()) 
                && 
                (executor->state == Executor::State::kRun)
            ) 
            {
                if (
                    executor->empty_condition.wait_until(lock, time) == std::cv_status::timeout                  // истек момент времени ожидания
                    &&
                    executor->threads_running_count + executor->threads_waiting_count > executor->low_watermark  // и при этом 
                ) 
                {
                    // если оказалось, что очередь пустая и потоков больше, чем low, то надо остановить поток, пусть он исчезнет
                    stop_thread = true;
                    break;
                }
            }
            
            // если не надо останавливать поток, то при наличии задачи в очереди - запустить на выполнение
            if (!stop_thread) 
            {
                if (executor->tasks.empty()) 
                {
                    continue; // перйти к ожиданию новой задачи
                }
                // чтобы извлечь задачу из очереди все еще удерживается мютекс
                task = executor->tasks.front();
                executor->tasks.pop_front();
                // пока мютех удерживается следует также изменить изменить значения счетчиков
                --(executor->threads_waiting_count);
                ++(executor->threads_running_count);
                // но чтобы запустить задачу, которая будет долго выполняться, мютех надо отпустить
                // поэтому запуск задачи сделать за пределами скобок
            }
        }
        if (!stop_thread) {
            // запуск задачи
            try {
                task();
            } catch (...) {
                // должна работать без ошибок
            }
        }
        // что делать, если поток выполнил задачу?
        // в этом случае надо изменить значения счетчиков
        // 
        {
            std::unique_lock<std::mutex> lock(executor->mutex);
            if(!stop_thread){
              ++(executor->threads_waiting_count);
              // если же надо остановить лишний поток, то не нужно этот счетчик менять
            }
            // но в любом случае уменьшить количество работающих потоков, так как поток выполнил задачу
            --(executor->threads_running_count);
            
            if (
                executor->state == Executor::State::kStopping 
                && 
                executor->tasks.empty()
            ) 
            {
                executor->state = Executor::State::kStopped;
                executor->stop_condition.notify_all();
                break;
            }
        }
        
        // отсюда поток начнет новый цикл в ожидании задачи из очереди
        
    }//while (!stop_thread) 
    
    // если выполнение оказывается здесь, то поток завершается и исчезает

    
}

void Executor::Stop(bool await) {
    std::unique_lock<std::mutex> lock(mutex);
    if (state == State::kRun) 
    {
        state = State::kStopping;
    }
    if (await && (threads_running_count > 0)) 
    {
        stop_condition.wait(lock, [&]() { return threads_running_count == 0; });
    } 
    else if (threads_running_count == 0) 
    {
        state = State::kStopped;
    }
}

    
    
    
    
    
    
    
    
    
    
} //namespace Concurrency
} // namespace Afina
