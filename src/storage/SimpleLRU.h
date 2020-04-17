#ifndef AFINA_STORAGE_SIMPLE_LRU_H
#define AFINA_STORAGE_SIMPLE_LRU_H

#include <map>
#include <memory>
#include <string>

#include <afina/Storage.h>


namespace Afina {
    namespace Backend {

        class SimpleLRU : public Afina::Storage
        {
        public:
            SimpleLRU(std::size_t max_size = 1024) : _max_size(max_size) {}

            ~SimpleLRU();

            // Implements Afina::Storage interface
            bool Put(const std::string& key, const std::string& value) override;

            // Implements Afina::Storage interface
            bool PutIfAbsent(const std::string& key, const std::string& value) override;

            // Implements Afina::Storage interface
            bool Set(const std::string& key, const std::string& value) override;

            // Implements Afina::Storage interface
            bool Delete(const std::string& key) override;

            // Implements Afina::Storage interface
            bool Get(const std::string& key, std::string& value) override;

        private:
            // LRU cache node
            using lru_node = struct lru_node
            {
                const std::string key;
                std::string value;
                lru_node* prev;
                std::unique_ptr<lru_node> next;
                lru_node(const std::string& key_, const std::string& value_) :
                    key(key_),
                    value(value_),
                    prev(nullptr),
                    next(nullptr)
                {}
            };


            // Maximum number of bytes could be stored in this cache.
            // i.e all (keys+values) must be less the _max_size
            std::size_t _max_size;
            std::size_t sz_current = 0;

            // Main storage of lru_nodes, elements in this list ordered descending by "freshness": in the head
            // element that wasn't used for longest time.
            //
            // List owns all nodes
            std::unique_ptr<lru_node> _lru_head;
            lru_node* tail = nullptr;


            // Index of nodes from list above, allows fast random access to elements by lru_node#key
            // !здесь по отношению к исходному коду изменено c <std::string> на <const std::string>
            using map_index = std::map<std::reference_wrapper<const std::string>, std::reference_wrapper<lru_node>, std::less<const std::string>>;
            map_index _lru_index;

            // !добавить вспомогательные функции для того, чтобы большие участки кода не дублировались

            void insert_new_node_to_tail(const std::string& key, const std::string& value);
            
            void modyfy_value_of_existing_node(map_index::iterator& it, const std::string& new_value);

            void move_node_to_tail(lru_node& node);

            void remove_node(lru_node& node);


        };

    } // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_SIMPLE_LRU_H
