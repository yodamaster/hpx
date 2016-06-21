//  Copyright (c) 2015 Anton Bikineev
//  Copyright (c) 2014 Thomas Heller
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#ifndef HPX_SERIALIZATION_POLYMORPHIC_ID_FACTORY_HPP
#define HPX_SERIALIZATION_POLYMORPHIC_ID_FACTORY_HPP

#include <hpx/config.hpp>
#include <hpx/runtime/serialization/detail/polymorphic_intrusive_factory.hpp>
#include <hpx/runtime/serialization/serialization_fwd.hpp>
#include <hpx/throw_exception.hpp>
#include <hpx/traits/polymorphic_traits.hpp>
#include <hpx/util/assert.hpp>
#include <hpx/util/static.hpp>

#include <boost/atomic.hpp>
#include <boost/mpl/bool.hpp>
#include <boost/preprocessor/stringize.hpp>

#include <unordered_map>
#include <string>
#include <vector>

#include <hpx/config/warnings_prefix.hpp>

namespace hpx { namespace serialization { namespace detail {
    struct typename_registry
    {
        typedef void* (*ctor_type)();

        HPX_STATIC_CONSTEXPR boost::uint32_t invalid_id = ~0u;

        typename_registry()
          : ctor_(nullptr)
          , id_(invalid_id)
        {}

        explicit typename_registry(ctor_type ctor)
          : ctor_(ctor)
          , id_(invalid_id)
        {}

        explicit typename_registry(boost::uint32_t id)
          : ctor_(nullptr)
          , id_(id)
        {}

        typename_registry(ctor_type ctor, boost::uint32_t id)
          : ctor_(ctor)
          , id_(id)
        {}

        bool operator==(typename_registry const& other)
        {
            return ctor_ == other.ctor_ && id_ == other.id_;
        }

        ctor_type ctor_;
        boost::uint32_t id_;
    };
}}}

namespace hpx { namespace serialization {

    namespace detail
    {
        class id_registry
        {
            HPX_NON_COPYABLE(id_registry);

        public:
            typedef typename_registry::ctor_type ctor_type;
            typedef std::unordered_map<std::string, typename_registry> typename_map_type;
            typedef std::vector<ctor_type> cache_type;

            HPX_STATIC_CONSTEXPR boost::uint32_t invalid_id = ~0u;

            void register_factory_function(const std::string& type_name,
                ctor_type ctor)
            {
                typename_registry& entry = typename_map_[type_name];
                entry.ctor_ = ctor;

                // populate cache
                if(entry.id_ != typename_registry::invalid_id)
                {
                    cache_id(entry.id_, entry.ctor_);
                }
            }

            void register_typename(const std::string& type_name,
                boost::uint32_t id)
            {
                typename_registry& entry = typename_map_[type_name];
                entry.id_ = id;

                // populate cache
                if(entry.ctor_ != nullptr)
                {
                    cache_id(entry.id_, entry.ctor_);
                }

                if (id > max_id) max_id = id;
            }

            boost::uint32_t try_get_id(const std::string& type_name) const
            {
                typename_map_type::const_iterator it =
                    typename_map_.find(type_name);
                if (it == typename_map_.end())
                    return typename_registry::invalid_id;

                return it->second.id_;
            }

            boost::uint32_t get_max_registered_id() const
            {
                return max_id;
            }

            std::vector<std::string> get_unassigned_typenames() const
            {
                typedef typename_map_type::value_type value_type;

                std::vector<std::string> result;

                for (const value_type& v : typename_map_)
                    if (v.second.id_ == typename_registry::invalid_id)
                        result.push_back(v.first);

                return result;
            }

            HPX_EXPORT static id_registry& instance();

        private:
            id_registry() : max_id(0u) {}

            friend struct ::hpx::util::static_<id_registry>;
            friend class polymorphic_id_factory;

            void cache_id(boost::uint32_t id, ctor_type ctor)
            {
                if (id >= cache.size()) //-V104
                    cache.resize(id + 1, nullptr); //-V106
                cache[id] = ctor; //-V108
            }

            boost::uint32_t max_id;
            typename_map_type typename_map_;
            cache_type cache;
        };

        class polymorphic_id_factory
        {
            HPX_NON_COPYABLE(polymorphic_id_factory);

            typedef typename_registry::ctor_type ctor_type;
            typedef id_registry::cache_type cache_type;

        public:
            template <class T>
            static T* create(boost::uint32_t id)
            {
                const cache_type& vec = id_registry::instance().cache;

                if (id > vec.size()) //-V104
                    HPX_THROW_EXCEPTION(serialization_error
                      , "polymorphic_id_factory::create"
                      , "Unknown type descriptor " + std::to_string(id));

                ctor_type ctor = vec[id]; //-V108
                HPX_ASSERT(ctor != nullptr);
                return static_cast<T*>(ctor());
            }

            static boost::uint32_t get_id(const std::string& type_name)
            {
                boost::uint32_t id = id_registry::instance().
                    try_get_id(type_name);

                if (id == id_registry::invalid_id)
                    HPX_THROW_EXCEPTION(serialization_error
                      , "polymorphic_id_factory::get_id"
                      , "Unknown typename: " + type_name);

                return id;
            }

        private:
            polymorphic_id_factory() {}

            HPX_EXPORT static polymorphic_id_factory& instance();

            friend struct hpx::util::static_<polymorphic_id_factory>;
        };

        template <class T>
        struct register_class_name<T, typename boost::enable_if<
            traits::is_serialized_with_id<T> >::type>
        {
            register_class_name()
            {
                id_registry::instance().register_factory_function(
                    T::hpx_serialization_get_name_impl(),
                    &factory_function);
            }

            static void* factory_function()
            {
                return new T;
            }

            register_class_name& instantiate()
            {
                return *this;
            }

            static register_class_name instance;
        };

        template <class T>
        register_class_name<T, typename boost::enable_if<
            traits::is_serialized_with_id<T> >::type>
                register_class_name<T, typename boost::enable_if<
                    traits::is_serialized_with_id<T> >::type>::instance;

        template <boost::uint32_t desc>
        std::string get_constant_entry_name();

        template <boost::uint32_t Id>
        struct add_constant_entry
        {
            add_constant_entry()
            {
                id_registry::instance().register_typename(
                        get_constant_entry_name<Id>(), Id);
            }

            static add_constant_entry instance;
        };

        template <boost::uint32_t Id>
        add_constant_entry<Id> add_constant_entry<Id>::instance;

    } // detail

}}

#include <hpx/config/warnings_suffix.hpp>

#define HPX_SERIALIZATION_ADD_CONSTANT_ENTRY(String, Id)                       \
    namespace hpx { namespace serialization { namespace detail {               \
        template <> std::string get_constant_entry_name<Id>()                  \
        {                                                                      \
            return BOOST_PP_STRINGIZE(String);                                 \
        }                                                                      \
        template add_constant_entry<Id>                                        \
            add_constant_entry<Id>::instance;                                  \
    }}}                                                                        \
/**/

#endif
