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

        // расчистить необходимое место в хранилище
        bool SimpleLRU::free_up_storage_space(std::size_t size_required)
        {
            if ( size_required > _max_size ) return false;

            while ( size_required > ( _max_size - sz_current ) ) {
                remove_node(*_lru_head.get());
            }

            return true;
        }


        // добавить в хранилище новую запись, при этом заведомо не должно быть там записи с key
        void SimpleLRU::insert_new_node_to_tail(const std::string& key, const std::string& value)
        {
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
            sz_current += ( key.size() + value.size() );
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

            /** далее можно считать, что есть как минимум два lru_node в списке
            * при этом выполнены такие условия
            *  tail != lru_head->get()
            *  node.next != nullptr, иначе он был бы последним, то есть tail
            **/

            if ( &node == _lru_head.get() ) {
                _lru_head.swap(node.next); // node.next - показывает на себя
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
            if ( !free_up_storage_space(key.size() + value.size()) ) return false;

            auto it = _lru_index.find(key);

            if ( it == _lru_index.end() ) { // вариант 1: нет записи с key => добавить новый key-value
                insert_new_node_to_tail(key, value);
            }
            else {                          // вариант 2: изменить value в записи с key
                it->second.get().value = value;
                sz_current += value.size();
                sz_current -= it->second.get().value.size(); // на всякий случай, если value.size()<it->second.get().value.size()
            }
            return true;
        }


        bool SimpleLRU::PutIfAbsent(const std::string& key, const std::string& value)
        {
            if ( _lru_index.find(key) != _lru_index.end() ) return false;
            return Put(key, value);
        }


        bool SimpleLRU::Set(const std::string& key, const std::string& value)
        {
            if ( _lru_index.find(key) == _lru_index.end() ) return false;
            return Put(key, value);
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

            // поднять рейтинг записи после каждого обращения
            move_node_to_tail(it->second.get());

            return true;
        }


    } // namespace Backend
} // namespace Afina
