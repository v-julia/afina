#include "SimpleLRU.h"
#include <memory>

namespace Afina {
    namespace Backend {

     // See MapBasedGlobalLockImpl.h
        bool SimpleLRU::Put(const std::string& key, const std::string& value)
        {

            std::size_t sz_new_add = key.size() + value.size();

            if ( sz_new_add > _max_size ) return false; // в любом случае превышает лимит

            // теперь новую запись всегда можно добавить в список, в том числе за счет удаления остальных

            // рассмотрим два варианта: 
            // вариант 1: добавление новой записи с этим ключом
            // вариант 2: замена значения, если запись с таким ключом уже есть

            //std::map<std::reference_wrapper<std::string>, std::reference_wrapper<lru_node>, std::less<std::string>>::iterator it;
            auto it = _lru_index.find(const_cast<std::string&>( key ));
            //auto it = _lru_index.find(std::ref( key ));

            if ( it == _lru_index.end() ) {
                // вариант 1: нет записи с key => добавить новый key-value
                // вначале освободить место, чтобы не превысить размер хранилища
                while ( sz_new_add > ( _max_size - sz_current ) ) {
                    std::size_t sz_free = _lru_head->key.size() + _lru_head->value.size();
                    _lru_index.erase(_lru_head->key);
                    _lru_head = std::move(_lru_head->next);
                    _lru_head->prev = nullptr;
                    sz_current -= sz_free;
                }
                // добавить запись
                if ( tail == nullptr ) {
                    _lru_head = std::move(std::unique_ptr<lru_node>(new lru_node)); //std::make_unique<lru_node>());//c++14
                    _lru_head->key = key;
                    _lru_head->value = value;
                    _lru_head->prev = nullptr;
                    tail = _lru_head.get();
                }
                else {
                    tail->next = std::move(std::unique_ptr<lru_node>(new lru_node));
                    tail->next->prev = tail;
                    tail = tail->next.get();
                    tail->key = key;
                    tail->value = value;
                }
                std::pair<std::reference_wrapper<std::string>, std::reference_wrapper<lru_node>> g(tail->key, *tail);
                _lru_index.emplace(g);
                sz_current += sz_new_add;
            }
            else {
                // вариант 2: записи с key есть => заменить новым value
                // аналогично - вначале освободить место
                // вычислить: сколько потребуется свободного места
                sz_new_add -= it->second.get().key.size();
                std::unique_ptr<lru_node>* node_for_remove = &_lru_head;
                while ( sz_new_add > ( _max_size - sz_current ) ) {
                    if ( node_for_remove->get()->key != key ) {
                        std::size_t sz_free = node_for_remove->get()->key.size() + node_for_remove->get()->value.size();
                        _lru_index.erase(node_for_remove->get()->key);
                        sz_current -= sz_free;
                        *node_for_remove = std::move(node_for_remove->get()->next);
                    }
                    else {
                        node_for_remove = &( node_for_remove->get()->next );
                    }
                }
                // теперь заменить значение
                it->second.get().value = value;
                sz_current += sz_new_add;
            }

            return true;
        }

        // See MapBasedGlobalLockImpl.h
        bool SimpleLRU::PutIfAbsent(const std::string& key, const std::string& value)
        {

            // также как и Put, но без замены уже существующей записи

            std::size_t sz_new_add = key.size() + value.size();

            if ( sz_new_add > _max_size ) return false;


            std::map<std::reference_wrapper<std::string>, std::reference_wrapper<lru_node>, std::less<std::string>>::iterator it;
            it = _lru_index.find(const_cast<std::string&>( key ));

            if ( it == _lru_index.end() ) {
                while ( sz_new_add > ( _max_size - sz_current ) ) {
                    std::size_t sz_free = _lru_head->key.size() + _lru_head->value.size();
                    _lru_index.erase(_lru_head->key);
                    _lru_head = std::move(_lru_head->next);
                    _lru_head->prev = nullptr;
                    sz_current -= sz_free;
                }
                // добавить запись
                if ( tail == nullptr ) {
                    _lru_head = std::move(std::unique_ptr<lru_node>(new lru_node)); //std::make_unique<lru_node>());//c++14
                    _lru_head->key = key;
                    _lru_head->value = value;
                    _lru_head->prev = nullptr;
                    tail = _lru_head.get();
                }
                else {
                    tail->next = std::move(std::unique_ptr<lru_node>(new lru_node));
                    tail->next->prev = tail;
                    tail = tail->next.get();
                    tail->key = key;
                    tail->value = value;
                }
                std::pair<std::reference_wrapper<std::string>, std::reference_wrapper<lru_node>> g(tail->key, *tail);
                _lru_index.emplace(g);
                sz_current += sz_new_add;
            }
            else {
                return false;
            }

            return true;
        }

        // See MapBasedGlobalLockImpl.h
        bool SimpleLRU::Set(const std::string& key, const std::string& value)
        {

           // также как и Put, но только для существующей записи с таким key

            std::size_t sz_new_add = key.size() + value.size();

            if ( sz_new_add > _max_size ) return false; // в любом случае превышает лимит

            std::map<std::reference_wrapper<std::string>, std::reference_wrapper<lru_node>, std::less<std::string>>::iterator it;
            it = _lru_index.find(const_cast<std::string&>( key ));

            if ( it == _lru_index.end() ) {
                // нет такой записи с key => false
                return false;
            }
            else {
                // запись с key есть => заменить новым value
                // наверное здесь нельзя удалять старые записи, а только изменить заданную
                // при этом должно остаться место в хранилище
                sz_new_add -= ( it->second.get().value.size() + key.size() );
                if ( sz_new_add > ( _max_size - sz_current ) ) return false;
                it->second.get().value = value;
                sz_current += sz_new_add;
                //// поднять рейтинг записи после каждого обращения
                //if ( it->second.get().next ) {
                //    it->second.get().next.swap(it->second.get().next->next);
                //    std::swap(it->second.get().prev, it->second.get().next->prev);
                //}
            }

            return true;
        }

        // See MapBasedGlobalLockImpl.h
        bool SimpleLRU::Delete(const std::string& key)
        {

            std::map<std::reference_wrapper<std::string>, std::reference_wrapper<lru_node>, std::less<std::string>>::iterator it;
            it = _lru_index.find(const_cast<std::string&>( key ));

            if ( it == _lru_index.end() ) return false; // не найден ключ

            // удалить запись с заданным ключом
            lru_node& node_for_delete = it->second.get();
            std::size_t sz_free = node_for_delete.key.size() + node_for_delete.value.size();
            // из индекса
            _lru_index.erase(it->second.get().key);

            // из хранилища
            if ( _lru_index.size() == 0 ) {
                _lru_head.reset();
                tail = nullptr;
                sz_current = 0;
            }
            else {
                if ( node_for_delete.prev == nullptr ) {
                    _lru_head = std::move(_lru_head->next);
                    _lru_head->prev = nullptr;
                }
                else {
                    lru_node* lru = node_for_delete.prev;
                    lru->next=std::move(node_for_delete.next);
                    if ( lru->next ) {
                        lru->next->prev = lru;
                    }
                    else {
                        tail = lru;
                    }
                }
                sz_current -= sz_free;
            }
            return true;
        }

        // See MapBasedGlobalLockImpl.h
        bool SimpleLRU::Get(const std::string& key, std::string& value)
        {

            std::map<std::reference_wrapper<std::string>, std::reference_wrapper<lru_node>, std::less<std::string>>::iterator it;
            it = _lru_index.find(const_cast<std::string&>( key ));

            if ( it == _lru_index.end() ) return false; // не найден ключ

            value = it->second.get().value;
            //// поднять рейтинг записи после каждого обращения
            //if ( it->second.get().next ) {
            //    it->second.get().next.swap(it->second.get().next->next);
            //    std::swap(it->second.get().prev, it->second.get().next->prev);
            //}

            return true;
        }

    } // namespace Backend
} // namespace Afina
