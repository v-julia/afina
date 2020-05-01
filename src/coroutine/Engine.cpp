#include <afina/coroutine/Engine.h>

#include <stdio.h>
#include <string.h>
#include <string.h>


namespace Afina {
namespace Coroutine {

void Engine::Store(context& ctx)
{
    // ����� ��������� ��������� ���������� ������ ����� 
    // ��� ����� - �������� ��������� ������� ������ � heap

    // � StackBottom ����� ������� ��������� ������������ ������ ����� (start)
    // � �������� ������ ��������� ���� ����������� � �� ������ ������ ��� ����� ������
    // ��� �� ��� � � start ��������� ������� ��������� �����, ���������� ������ ��� ������������ ���������
    // ���� �� ���� ���������� ������ ����  (����������� ������) ��� ����� (������������� ������) - ������� �� ���������

    char NowStackHere;

    // ������ ������� ���, ����� ������ ����� ���� ������, ��������� ������
    if ( StackBottom < &NowStackHere ) {
        ctx.Low = StackBottom;
        ctx.Hight = &NowStackHere;
    }
    else {
        ctx.Low = &NowStackHere;
        ctx.Hight = StackBottom;
    }

    uint32_t to_stored_data_size = ctx.Hight - ctx.Low + 1;

    char* &addr_of_availabe_storage = std::get<0>(ctx.Stack);   // ��� ������ �� ������ ������� tuple Stack
    auto &size_of_available_store = std::get<1>(ctx.Stack);

    // ���� ����� �������, �� ����� ����� ���������� ������
    // ����� ���� �������� ����� ������ ��� �����
    if ( to_stored_data_size > size_of_available_store ) {
        free(addr_of_availabe_storage);
        addr_of_availabe_storage = (char*)malloc(to_stored_data_size);
        //delete[] addr_of_availabe_storage;
        //addr_of_availabe_storage = new char[to_stored_data_size];
        size_of_available_store = to_stored_data_size; // ��������� - ������� �������� ������ ��� ��������
    }
    memcpy((void*)addr_of_availabe_storage, (const void*)ctx.Low, ( std::size_t )to_stored_data_size);

    // Environment ������������ ��� ������ setjmp
}

void Engine::Restore(context& ctx)
{
    // ����� ��������� ������������ ������������ ���������� �����
    // ������������� � ctx ������ �����
    //   - ��� ������ ���� ��������� � �������� ������� �� ctx.Low �� ctx.Hight
    //   - ���������� ��������� �� ������ std::get<0>(ctx.Stack) � ��� ������ ��� ctx.Hight-ctx.Low

    char NowStackHere; // ��������� ������� ����� ���������� � ������ ������

    if ( ctx.Low <= &NowStackHere && &NowStackHere <= ctx.Hight ) {
        Restore(ctx);
        // ���������� ������� �� ������� �������, ������� ���������� ������������
        // � ����������� �� ����, � ����� ������� ������ ���� - �������� ������ ������� ��� �������
    }

    memcpy((void*)ctx.Low, (const void*)std::get<0>(ctx.Stack), ( std::size_t )( ctx.Hight - ctx.Low + 1 ));

    cur_routine = &ctx;

    // ������� ���������� � ����� ���������� ��������� setjmp � ������������ ��������� 1
    longjmp(ctx.Environment, 1);

    // ����� �� ��������� ������������ ��� stack  1-8 Mb ?
}

void Engine::yield()
{
    /**
     *  ������� �����-�� ������, �� ���������� ������� � ��������� ��.
     *  ���� ����� ������ �� ����������, �� ������� ������ �� ������.
     *  ����� ���� ������� ������ �� ��������
     */

    // ��������� ���� ��������, ��� ������ �� �������� ���������� �������� cur_routine
    // �� ���� ������������� � ����� ������ ��� ������, ���� �� �����, �� ���������� ��������� 

    if ( alive == nullptr ) return; // �� ����� �� ������� ������ ������ - ������ ���������� � �������� �������

    // ����� ������ ��� ������ ������ �������� (�������� cur_routine != nullptr)
    // � �������� �� ����� ����� ������, ����� ����� ��������� �� cur_routine
    context* other_routine = ( cur_routine->next ) ? cur_routine->next : alive;

    if ( other_routine == cur_routine ) {
        return;
    }
    // ������, ����� ��������� ������ ��������
    // �������������� ��������� ��������� ����

    if ( cur_routine != idle_ctx ) {
        Store(*cur_routine);
        if ( setjmp(cur_routine->Environment) ) {
            // ���� ����� �������� ���������� ��������, ����� ��� � ��������� ��� ����� ��������
            // �� ��� ��� ���������, ������� ����� ����������� � �������� ����� ��, ���
            // ���� � ������ ����������, �� ���� ������ ������� ���������� ������� � ��������
            return;
        }
    }
    cur_routine = other_routine;
    Restore(*other_routine);
}

void Engine::sched(void* routine_)
{
    /**
     *  ������������� ������� ������ � ��������� ��, ������� ������ ����������.
     *  ����� ���� ������� ������ �� ��������
     */

    // �� ������ �������, ������ void*, � �� context
    if ( routine_ == nullptr ) {
        yield(); // ���� ������, ����� ������ � ���������
    }

    if ( (void*)cur_routine == routine_ ) {
        yield();  // ���� ���� � �� ��
    }


    // � �������� - void* routine_ - ��� ��������� �� ��������, � �� �� �������
    context* started_routine = (context*)routine_; // ������� ��������� �� ������� ����������� ��� ��������


    // ������ ���� �� ������, ���� started_routine � ������ alive ��� ���
    // ���� �� ��� � ������, �� �� ���������

    context* any_context = alive;
    while ( any_context != started_routine && any_context != nullptr ) {
        any_context = any_context->next;
    }
    if ( any_context == nullptr ) {
        return;
    }
    // ��������� �������� ������� ���������, ������� �����������
    if ( cur_routine != nullptr && cur_routine != idle_ctx ) {
            // � Engine.h ���� Restore(*idle_ctx), ��� ����� �������� � ����, ��� cur_routine �������� ������ idle_ctx
            Store(*cur_routine);
            if ( setjmp(cur_routine->Environment) ) {
            return;// � ��� ����� ���������� ��������, ����� ���� ����� ���������� ���������� cur_routine
        }
    }

    // � ����� ������ started_routine ������� ��� �������� ����������, 
    // ������� ������������ ��� ������� � ���������� ���������� �� ���
    cur_routine = started_routine;
    Restore(*cur_routine);

}

} // namespace Coroutine
} // namespace Afina
