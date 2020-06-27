#include <afina/coroutine/Engine.h>

#include <stdio.h>
#include <string.h>
#include <string.h>


namespace Afina {
namespace Coroutine {

void Engine::Store(context& ctx)
{
    // здесь требуется запомнить содержимое только стека 
    // для этого - выделить некоторый участок памяти в heap

    // в StackBottom перед вызовом программы записывается начало стека (start)
    // в процессе работы программы стек заполняется и на данный момент его можно узнать
    // так же как и в start обозначив текущее положение стека, выделением памяти под однобайтовую переменну
    // стек по мере заполнения растет вниз  (уменьшаются адреса) или вверх (увеличиваются адреса) - зависит от платформы

    char NowStackHere;

    // теперь сделать так, чтобы начало стека было меньше, окончание больше
    if ( StackBottom < &NowStackHere ) {
        ctx.Low = StackBottom;
        ctx.Hight = &NowStackHere;
    }
    else {
        ctx.Low = &NowStackHere;
        ctx.Hight = StackBottom;
    }

    uint32_t to_stored_data_size = ctx.Hight - ctx.Low + 1;

    char* &addr_of_availabe_storage = std::get<0>(ctx.Stack);   // это ссылка на первый элемент tuple Stack
    auto &size_of_available_store = std::get<1>(ctx.Stack);

    // если места хватает, то здесь можно разместить данные
    // иначе надо выделить новую память для стека
    if ( to_stored_data_size > size_of_available_store ) {
        free(addr_of_availabe_storage);
        addr_of_availabe_storage = (char*)malloc(to_stored_data_size);
        //delete[] addr_of_availabe_storage;
        //addr_of_availabe_storage = new char[to_stored_data_size];
        size_of_available_store = to_stored_data_size; // запомнить - сколько выделили памяти под хранение
    }
    memcpy((void*)addr_of_availabe_storage, (const void*)ctx.Low, ( std::size_t )to_stored_data_size);

    // Environment запоминается при вызове setjmp
}

void Engine::Restore(context& ctx)
{
    // здесь требуется восстановить сохраненнное содержимое стека
    // запомнившиеся в ctx данные такие
    //   - где раньше стек находился в пределах адресов от ctx.Low до ctx.Hight
    //   - содержимое хранилось по адресу std::get<0>(ctx.Stack) и его размер был ctx.Hight-ctx.Low

    char NowStackHere; // последний элемент стека выполнения в данный момент

    if ( ctx.Low <= &NowStackHere && &NowStackHere <= ctx.Hight ) {
        Restore(ctx);
        // рекурсивно выходим за пределы области, которую собираемся восстановить
        // в зависимости от того, в какую сторону растет стек - перейдет нижнюю границу или верхнюю
    }

    memcpy((void*)ctx.Low, (const void*)std::get<0>(ctx.Stack), ( std::size_t )( ctx.Hight - ctx.Low + 1 ));

    cur_routine = &ctx;

    // вернуть выполнение в точку сохранения контекста setjmp с возвращаемым значением 1
    longjmp(ctx.Environment, 1);

    // может ли произойти переполнение для stack  1-8 Mb ?
}

void Engine::yield()
{
    /**
     *  Находит какую-то задачу, не являющуюся текущей и запускает ее.
     *  Если такой задачи не существует, то функция ничего не делает.
     *  Может быть вызвана только из корутины
     */

    // попадание сюда означает, что пришли из середины работающей корутины cur_routine
    // ее надо приостановить и найти вместо нее другую, если не найти, то продолжить выполнять 

    if ( alive == nullptr ) return; // по какой то причине список пустой - вернет выполнение в корутину обратно

    // поиск годной для работы другой корутины (заведомо cur_routine != nullptr)
    // в принципе не стоит долго искать, можно взять следующую за cur_routine
    context* other_routine = ( cur_routine->next ) ? cur_routine->next : alive;

    if ( other_routine == cur_routine ) {
        return;
    }
    // значит, можно запустить другую корутину
    // предварительно сохранить состояние этой

    if ( cur_routine != idle_ctx ) {
        Store(*cur_routine);
        if ( setjmp(cur_routine->Environment) ) {
            // сюда будет передано выполнение корутины, когда она в следующий раз будет запущена
            // но так как состояние, которое здесь запомнилось в точности такое же, как
            // было в момент прерывания, то надо просто вернуть выполнение обратно в корутину
            return;
        }
    }
    cur_routine = other_routine;
    Restore(*other_routine);
}

void Engine::sched(void* routine_)
{
    /**
     *  Останавливает текущую задачу и запускает ту, которая задана аргументом.
     *  Может быть вызвана только из корутины
     */

    // не вполне понятно, почему void*, а не context
    if ( routine_ == nullptr ) {
        yield(); // если пустая, найти другую и запустить
    }

    if ( (void*)cur_routine == routine_ ) {
        yield();  // если одна и та же
    }


    // в принципе - void* routine_ - это указатель на контекст, а не на функцию
    context* started_routine = (context*)routine_; // попытка указатель на функцию рассмотреть как контекст


    // теперь надо бы узнать, есть started_routine в списке alive или нет
    // если ее нет в списке, то не выполнять

    context* any_context = alive;
    while ( any_context != started_routine && any_context != nullptr ) {
        any_context = any_context->next;
    }
    if ( any_context == nullptr ) {
        return;
    }
    // сохранить контекст текущей программы, которая выполнялась
    if ( cur_routine != nullptr && cur_routine != idle_ctx ) {
            // в Engine.h есть Restore(*idle_ctx), что может привести к тому, что cur_routine окажется равным idle_ctx
            Store(*cur_routine);
            if ( setjmp(cur_routine->Environment) ) {
            return;// в эту точку выполнение вернется, когда надо будет продолжить выполнение cur_routine
        }
    }

    // в любом случае started_routine годится для передачи выполнения, 
    // поэтому обозначается как текущая и передается выполнение на нее
    cur_routine = started_routine;
    Restore(*cur_routine);

}

} // namespace Coroutine
} // namespace Afina
