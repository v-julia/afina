#include "SimpleLRU.h"
#include <memory>

namespace Afina {
namespace Backend {

SimpleLRU::~SimpleLRU()
{
    _lru_index.clear();
    if ( _lru_head ) {
        lru_node* lru;
        while ( ( lru = tail->prev ) != nullptr ) {
            lru->next.reset();
            tail = lru;
        }
        _lru_head.reset();
    }
}


// добавить в хранилище новую запись, при этом заведомо не должно быть там записи с key
void SimpleLRU::insert_new_node_to_tail(const std::string& key, const std::string& value)
{
    size_t sz_required = key.size() + value.size();
    while ( sz_required > ( _max_size - sz_current ) ) remove_node(*_lru_head.get());
    auto new_node = std::unique_ptr<lru_node>(new lru_node(key, value)); //std::make_unique<lru_node>());//c++14
    if ( !_lru_head ) {
        _lru_head = std::move(new_node);
        tail = _lru_head.get();
    }
    else {
        tail->next = std::move(new_node);
        tail->next->prev = tail;
        tail = tail->next.get();
    }
    std::pair<std::reference_wrapper<const std::string>, std::reference_wrapper<lru_node>> g(tail->key, *tail);
    _lru_index.emplace(g);
    sz_current += sz_required;
}


// модифицировать значение существующей записи, при этом запись с key должна быть, а места - достаточно
void SimpleLRU::modyfy_value_of_existing_node(map_index::iterator& it, const std::string& new_value)
{
    move_node_to_tail(it->second.get());
    while ( sz_current - it->second.get().value.size() + new_value.size() > _max_size ) remove_node(*_lru_head.get());
    it->second.get().value = new_value;
    sz_current -= it->second.get().value.size();
    sz_current += new_value.size();
}


// удаление из списка и из индекса (считается, что такой node есть)
void SimpleLRU::remove_node(lru_node& node)
{
    sz_current -= ( node.key.size() + node.value.size() );
    // из индекса
    _lru_index.erase(node.key);

    // из хранилища
    if ( _lru_index.size() == 0 ) {
        _lru_head.reset();
        tail = nullptr;
        sz_current = 0;
    }
    else if ( node.prev == nullptr ) {
        _lru_head = std::move(_lru_head->next);
        _lru_head->prev = nullptr;
    }
    else {
        lru_node* lru = node.prev;
        lru->next = std::move(node.next);
        if ( lru->next ) {
            lru->next->prev = lru;
        }
        else {
            tail = lru;
        }
    }
}


void SimpleLRU::move_node_to_tail(lru_node& node)
{
    if ( &node == tail ) return;

    if ( &node == _lru_head.get() ) {
        _lru_head.swap(node.next); // node.next - показывает на себя (на node)
        _lru_head->prev = nullptr;
    }
    else {
        node.next->prev = node.prev;
        node.prev->next.swap(node.next); // точно также node.next - показывает на себя
    }
    tail->next.swap(node.next); // теперь за tail следует node
    node.prev = tail;           // предшествующий к node - tail (фактически node становится последним)
    tail = &node;               // формально обозначить node как tail
}




bool SimpleLRU::Put(const std::string& key, const std::string& value)
{
    if ( ( key.size() + value.size() ) > _max_size ) return false;
    auto it = _lru_index.find(key);

    if ( it == _lru_index.end() )
        insert_new_node_to_tail(key, value);
    else
        modyfy_value_of_existing_node(it, value);

    return true;
}


bool SimpleLRU::PutIfAbsent(const std::string& key, const std::string& value)
{
    if ( ( key.size() + value.size() ) > _max_size ) return false;
    auto it = _lru_index.find(key);
    if ( _lru_index.find(key) != _lru_index.end() ) return false;
    insert_new_node_to_tail(key, value);
    return true;
}


bool SimpleLRU::Set(const std::string& key, const std::string& value)
{
    if ( ( key.size() + value.size() ) > _max_size ) return false;
    auto it = _lru_index.find(key);
    if ( _lru_index.find(key) == _lru_index.end() ) return false;
    modyfy_value_of_existing_node(it, value);
    return true;
}


bool SimpleLRU::Delete(const std::string& key)
{
    auto it = _lru_index.find(key);
    if ( it == _lru_index.end() ) return false; // не найден ключ
    remove_node(it->second.get());
    return true;
}


bool SimpleLRU::Get(const std::string& key, std::string& value)
{
    auto it = _lru_index.find(key);
    if ( it == _lru_index.end() ) return false; // не найден ключ
    value = it->second.get().value;
    move_node_to_tail(it->second.get());
    return true;
}


} // namespace Backend
} // namespace Afina
