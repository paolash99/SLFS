#pragma once

#ifndef CACHING_HPP__
#define CACHING_HPP__

#include <oneapi/tbb/concurrent_hash_map.h>
#include <oneapi/tbb/concurrent_unordered_map.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/concurrent_set.h>
#include <slsfs.hpp>
#include <optional>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>

namespace slsfsdf::cache
{

using frequency_map =
    oneapi::tbb::concurrent_hash_map<slsfs::pack::key_t,
                                     int,
                                     slsfs::uuid::hash_compare<slsfs::pack::key_t>>;

using block_map =
    oneapi::tbb::concurrent_hash_map<std::uint32_t,
                                     slsfs::base::buf>;

using caching_map =
    oneapi::tbb::concurrent_hash_map<slsfs::pack::key_t,
                                     block_map,
                                     slsfs::uuid::hash_compare<slsfs::pack::key_t>>;

class cache_entry
{
public:
    std::chrono::high_resolution_clock::time_point timestamp_ = std::chrono::high_resolution_clock::now();
    slsfs::pack::key_t const file_key_;
    std::uint32_t const block_id_;
    std::uint32_t const data_size_;

    cache_entry(slsfs::pack::key_t const& key, std::uint32_t b_id, std::uint32_t datasize):
        file_key_{key}, block_id_{b_id}, data_size_{datasize} {}

    bool operator== (cache_entry const & rhs) const
    {
        return file_key_ == rhs.file_key_ &&
               block_id_ == rhs.block_id_;
    }
};

// Comapre object for comparing cache_entry objects
struct cache_entry_compare
{
    bool operator() (cache_entry const& a, cache_entry const& b) const {
        return std::tie(a.file_key_, a.block_id_) < std::tie(b.file_key_, b.block_id_);
    }
};

// Implementation of an LRU cache
// TODO: use concurrent data structures instead of mutex
class LRUCache
{
    struct node
    {
        cache_entry key;
        int value;
        node * prev;
        node * next;

        node(cache_entry const& k, int val):
            key{k}, value {val} {}
    };

    std::mutex mtx_;

    node* head = new node(cache_entry(slsfs::pack::key_t{},0,0), -1);
    node* tail = new node(cache_entry(slsfs::pack::key_t{},0,0), -1);

    caching_map& caching_map_ref_;

    std::uint32_t cap;
    int cur_size_ = 0;

    std::map<cache_entry, node *, cache_entry_compare> m;

    void addnode(node * temp)
    {
        cur_size_ += temp->key.data_size_;
        slsfs::log::log("(caching.addnode) Cache space = {}/{}", cur_size_ * 0.001, cap * 0.001);

        node * dummy = head->next;
        head->next = temp;
        temp->prev = head;
        temp->next = dummy;
        dummy->prev = temp;
    }

    void deletenode(node * temp)
    {
        // reducing current size of cache by size of deleted cache entry
        cur_size_ -= temp->key.data_size_;
        slsfs::log::log("(caching.deletenode) Cache space = {}/{}", cur_size_ * 0.001, cap * 0.001);

        node * delnext = temp->next;
        node * delprev = temp->prev;
        delnext->prev = delprev;
        delprev->next = delnext;
        delete temp;
    }

    void delete_entry_from_cache(cache_entry entry)
    {
        slsfs::log::log("(caching.delete_entry_from_cache)");
        caching_map::accessor acc;
        if (caching_map_ref_.find(acc, entry.file_key_))
        {
            auto ret = acc->second.erase(entry.block_id_);
            if (ret)
            {
                evictions++;
                slsfs::log::log("(caching.delete_entry_from_cache) evictions so far = {}", evictions);
            }
            slsfs::log::log("(caching.delete_entry_from_cache) erasure of block {}: {}", entry.block_id_, ret);

            if (acc->second.size() == 0)
            {
                auto ret = caching_map_ref_.erase(acc);
                slsfs::log::log("(caching.delete_entry_from_cache) all blocks erased, erasing file from cache: {}", ret);
            }
        }
    }

public:

    // metrics
    long evictions = 0;

    // Constructor for initializing the
    // cache capacity with the given value.
    LRUCache(std::uint32_t capacity, caching_map& caching_map_ref) :
        caching_map_ref_(caching_map_ref)
    {
        cap = capacity;
        head->next = tail;
        tail->prev = head;
    }

    node * get_head() {
        return head;
    }

    node * get_tail() {
        return tail;
    }

    // This method works in O(1)
    void set(cache_entry key)
    {
        // TODO: synchronize this in a more efficient way 
        std::lock_guard<std::mutex> lock{mtx_};

        // if cache entry is found, delete existing to replace with new one
        if (m.find(key) != m.end())
        {
            node * exist = m[key];
            m.erase(key);
            deletenode(exist);
            // let emplace() replace existing entry in caching_map.block_map
        }

        while (cur_size_ + key.data_size_ > cap)
        {
            // delete this tail->prev->key from caching_map.block_map
            slsfs::log::log("(caching.set) in while loop, cur size = {}, key.data_size = {}", cur_size_, key.data_size_);
            delete_entry_from_cache(tail->prev->key);

            m.erase(tail->prev->key);
            deletenode(tail->prev);
        }

        addnode(new node(key, 0));
        m[key] = head->next;
    }
};

/**
 * Cache approach #1:
 * On first request, we fetch the whole file and place it in the cache.
 * subsequent writes only modify part of the file that they need to modify
 * writes are applied to the cache once they are commited to disk
 * reads can be taken from the cache.
 *
 * Cache approach #2: (IMPLEMENTING)
 * We cache what the first request asks for based on LFU.
 * So we do not cache the entire file unless the request asks for it.
 *
 *  edge case #1:
 *  we read part of a file that is not cached
 *
*/
class cache
{
    // cache
    caching_map caching_map_;
    // LFU map
    frequency_map frequency_map_;
    // LRU structure
    LRUCache lru_cache_;


    // Block size and block limit
    std::uint32_t const fullsize_ = 4096;
    std::uint32_t const block_limit_ = 104857600; // 100 megabytes
    std::atomic<std::uint32_t> block_counter_ = 0;
public:
    std::string eviction_policy_ = "LRU";
private:

    // Metrics
    long cache_hits_ = 0;

    auto blocksize() -> std::uint32_t { return fullsize_; }

    auto get_block_ids(slsfs::jsre::request_parser<slsfs::base::byte> const& input)
         -> std::vector<std::uint32_t>
    {
        std::uint32_t const realpos  = input.position();
        std::uint32_t const readsize = input.size();
        std::uint32_t const endpos   = realpos + readsize;

        if (readsize == 0)
            return {};

        std::vector<std::uint32_t> block_ids;

        for (std::uint32_t currentpos = realpos; currentpos < endpos; currentpos += blocksize())
            block_ids.push_back(currentpos / blocksize());

        return block_ids;
    }

public:
    cache(std::uint32_t const cachesize, std::string const& policy):
        lru_cache_(cachesize, caching_map_),
        block_limit_{cachesize},
        eviction_policy_{policy} {
        slsfs::log::log("(cache) cache size: {}, policy: {}", block_limit_, eviction_policy_);
    }

    ///////////////////////////// CACHE TRANSFER OPERATIONS ////////////////////////////////

    // Prepares cache and LRU tables for transfer
    auto get_tables() -> std::vector<slsfs::pack::unit_t>
    {
        std::vector<slsfs::pack::unit_t> toReturn;

        slsfs::log::log("(caching.get_tables) generating string of caching table");

        auto r = caching_map_.range();
        for (caching_map::iterator i = r.begin(); i != r.end(); i++)
        {
            slsfs::log::log("(caching.get_tables) i->first size: {}", i->first.end() - i->first.begin());
            std::copy (i->first.begin(), i->first.end(), std::back_inserter(toReturn));
            toReturn.push_back(':');

            auto r_b = i->second.range();
            for (block_map::iterator i_b = r_b.begin(); i_b != r_b.end(); i_b++)
            {
                std::array<std::uint8_t, 4> buf;
                slsfs::log::log("(caching.get_tables) first = {} ", i_b->first);
                std::memcpy(buf.data(), &i_b->first, 4);

                std::copy(buf.begin(), buf.end(), std::back_inserter(toReturn));

                std::uint32_t b_size = i_b->second.size();

                std::array<std::uint8_t, 4> buf_size;
                std::memcpy(buf_size.data(), &b_size, 4);

                std::copy(buf_size.begin(), buf_size.end(), std::back_inserter(toReturn));

                if (std::next(i_b) != r_b.end())
                    toReturn.push_back(',');
            }

            toReturn.push_back(' ');
        }

        // LRU Transfer table
        auto iterator = lru_cache_.get_tail()->prev;
        while (iterator != lru_cache_.get_head())
        {
            slsfs::log::log("(caching.get_tables) cache entry block_id: {}", iterator->key.block_id_);
            std::copy (iterator->key.file_key_.begin(), iterator->key.file_key_.end(), std::back_inserter(toReturn));
            toReturn.push_back('.');

            std::array<std::uint8_t, 4> buf;
            std::memcpy(buf.data(), &iterator->key.block_id_, 4);

            std::copy(buf.begin(), buf.end(), std::back_inserter(toReturn));

            std::array<std::uint8_t, 4> buf_size;
            std::memcpy(buf_size.data(), &iterator->key.data_size_, 4);

            std::copy(buf_size.begin(), buf_size.end(), std::back_inserter(toReturn));

            toReturn.push_back(' ');

            iterator = iterator->prev;
        }


        std::array<std::uint8_t, 4> buf;
        std::memcpy(buf.data(), &cache_hits_, 4);

        std::copy(buf.begin(), buf.end(), std::back_inserter(toReturn));

        std::memcpy(buf.data(), &lru_cache_.evictions, 4);
        std::copy(buf.begin(), buf.end(), std::back_inserter(toReturn));

        return toReturn;
    }

    // Builds the cache and LRU table from a transfer request
    void build_cache(std::vector<slsfs::pack::unit_t>& cache_table, std::shared_ptr<storage_conf> conf)
    {
        for (auto it = cache_table.begin(); it < cache_table.end(); )
        {
            slsfs::log::log("(caching.build_table) parsing key");
            slsfs::pack::key_t file_key;
            std::copy_n(it, 32, file_key.begin());
            std::advance(it, 32);
            block_map blocks;

            caching_map::const_accessor acc;
            if (!caching_map_.find(acc, file_key))
            {
                caching_map_.emplace(file_key, blocks);
                acc.release();
            }

            while (*it != ' ')
            {
                // skips comma or colon
                if (*it == '.')
                {
                    it++;

                    // reconstructing the bid from 4 bytes
                    std::uint32_t bid = 0;

                    std::array<std::uint8_t, 4> buf;
                    std::copy_n (it, 4, buf.begin());
                    std::memcpy(&bid, buf.data(), 4);

                    // skipping the bid 4 bytes
                    std::advance(it, 4);

                    std::uint32_t b_size = 0;

                    std::array<std::uint8_t, 4> buf_size;
                    std::copy_n (it, 4, buf_size.begin());
                    std::memcpy(&b_size, buf_size.data(), 4);

                    slsfs::log::log("(caching.build_table) LRU: bid={}, size={}", bid, b_size);

                    // skipping the size 4 bytes
                    std::advance(it, 4);

                    lru_cache_.set(cache_entry(file_key, bid, b_size));

                    break;
                }

                it++;

                // reconstructing the bid from 4 bytes
                std::uint32_t bid = 0;

                std::array<std::uint8_t, 4> buf;
                std::copy_n (it, 4, buf.begin());
                std::memcpy(&bid, buf.data(), 4);

                // skipping the bid 4 bytes
                std::advance(it, 4);

                std::uint32_t b_size = 0;

                std::array<std::uint8_t, 4> buf_size;
                std::copy_n (it, 4, buf_size.begin());
                std::memcpy(&b_size, buf_size.data(), 4);

                slsfs::log::log("(caching.build_table) CACHE: bid={}, size={}", bid, b_size);

                // skipping the size 4 bytes
                std::advance(it, 4);

                // Getting block data from backend
                slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
                ptr->header.gen();
                ptr->header.key = file_key;

                slsfs::jsre::request read_request {
                    .type      = slsfs::jsre::type_t::file,
                    .operation = slsfs::jsre::operation_t::read,
                    .position  = bid * 4096,
                    .size      = b_size
                };

                read_request.to_network_format();
                ptr->data.buf.resize(sizeof (read_request));
                std::memcpy(ptr->data.buf.data(), &read_request, sizeof (read_request));
                slsfs::jsre::request_parser<slsfs::base::byte> read_request_input {ptr};

                // calling backend to fetch block
                slsfs::log::log("(caching.build_table) starting perform bid={}", bid);
                conf->start_perform(
                    read_request_input,
                    [this, bid, read_request_input] (slsfs::base::buf data) {
                        slsfs::log::log("(caching.build_table) bid = {}, readsize = {}", bid, data.size());
                        caching_map::accessor acc;
                        if (caching_map_.find(acc, read_request_input.uuid()))
                        {
                            slsfs::log::log("(caching.build_table) emplacing block");
                            acc->second.emplace(bid, data);
                            block_counter_ += data.size();
                        }
                    });
            }
            // skip whitespace
            it++;
        }
    }


    ///////////////////////////// CACHE IO OPERATIONS ////////////////////////////////

    auto read_from_cache(slsfs::jsre::request_parser<slsfs::base::byte> const& input)
        -> std::optional<slsfs::base::buf>
    {
        caching_map::const_accessor acc;
        // Case 1: file is cached
        if (caching_map_.find(acc, input.uuid()))
        {
            slsfs::log::log("(read_from_cache) Cache hit on file");
            // Find needed block ids
            std::vector<std::uint32_t> block_ids = get_block_ids(input);

            slsfs::base::buf to_return;
            block_map::const_accessor block_acc;
            long block_c = 0;
            for (std::uint32_t const block_id : block_ids)
            {
                if (acc->second.find(block_acc, block_id))
                {
                    block_c++;
                    slsfs::log::log("(read_from_cache) Cache hit on block {}, size={}", block_id, block_acc->second.size());

                    if (eviction_policy_ != "FIFO")
                        lru_cache_.set(cache_entry(input.uuid(), block_id, block_acc->second.size()));

                    to_return.insert(
                        to_return.end(),
                        block_acc->second.begin(),
                        block_acc->second.end());
                }
                else
                {
                    slsfs::log::log("(read_from_cache) Cache miss on block {}", block_id);
                    return std::nullopt; // TODO: get missing blocks from SSBD in a map(block_id, buf) and insert it in the right order
                }
            }

            cache_hits_++;
            slsfs::log::log("(read_from_cache) Cache hit: {} blocks, hitss so far = {}", block_c, cache_hits_);
            return to_return;
        }
        // Case 3: CACHE MISS
        // Case 3a: cache miss because no buffer
        // Case 3b: cache miss because missing part of file
        slsfs::log::log("(read_from_cache) Cache miss on file");
        return std::nullopt;
    }

    template<typename CharType>
    void write_to_cache(slsfs::jsre::request_parser<slsfs::base::byte> const& input,
                        CharType * data)
    {
        /**
         * Writing to cache
         * Case 1: parts of the file are cached
         * case 1a: update cached parts
         * case 1b: add new parts of the file
         * Case 2: no part of the file has been cached yet
        **/
        caching_map::const_accessor acc;
        if (caching_map_.find(acc, input.uuid()))
        {
            std::vector<std::uint32_t> block_ids = get_block_ids(input);
            slsfs::log::log("(write_to_cache) file found.");

            for (std::uint32_t const block_id : block_ids)
            {
                slsfs::log::log("(write_to_cache) FOR loop iteration, block id from input: {}", block_id);
                slsfs::base::buf toEmplace;

                std::uint32_t const copy_size = std::min(blocksize(), input.size() - ((block_id - input.position() / blocksize()) * blocksize()));

                std::copy_n(data + (block_id - (input.position() / blocksize())) * blocksize(),
                            copy_size,
                            std::back_inserter(toEmplace));

                acc.release();
                slsfs::log::log("(write_to_cache) emplacing block");
                lru_cache_.set(cache_entry(input.uuid(), block_id, copy_size));
                caching_map::accessor acc_mod;
                if (caching_map_.find(acc_mod, input.uuid()))
                    acc_mod->second.emplace(block_id, toEmplace);
            }
        }
        else
        {
            slsfs::log::log("(write_to_cache) file not found. adding block map");
            block_map blocks;

            std::vector<std::uint32_t> block_ids = get_block_ids(input);
            for (std::uint32_t const block_id : block_ids)
            {
                slsfs::log::log("(write_to_cache) FOR loop iteration, block id from input: {}", block_id);
                slsfs::base::buf toEmplace;
                std::uint32_t copy_size = std::min(blocksize(), input.size() - ((block_id - input.position() / blocksize()) * blocksize()));

                std::copy_n(data + (block_id - (input.position() / blocksize())) * blocksize(), // crashes here
                            copy_size,
                            std::back_inserter(toEmplace));

                slsfs::log::log("(write_to_cache) emplacing block");
                lru_cache_.set(cache_entry(input.uuid(), block_id, copy_size));
                blocks.emplace(block_id, toEmplace);
                block_counter_ += copy_size;
            }

            caching_map_.emplace(input.uuid(), blocks);
        }
    }
};

} // namespace slsfsdf::cache

#endif // CACHING_HPP__
