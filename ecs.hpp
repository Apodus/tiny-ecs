#pragma once

#include <vector>
#include <type_traits>

#ifdef _WIN32
#include <intrin.h> // for bitscanforward
#pragma intrinsic(_BitScanForward64)
inline uint64_t findFirstSetBit(uint64_t source) {
	unsigned long out = 0;
	_BitScanForward64(&out, source);
	return out;
}
#else
inline uint64_t findFirstSetBit(uint64_t source) {
	static_assert(sizeof(unsigned long) == sizeof(uint64_t), "unsigned long is not 64bit on this system. rip. you probably need to use __builtin_ctzll instead.");
	return __builtin_ctzl(source);
}
#endif

namespace urtela {

	// TODO: thread safety?
	// TODO: separate entity_id type might be nice.
	class ecs {
	private:
		std::vector<void*> m_components;

		class type_index {
			uint64_t runningTypeIndex = 0;
			static constexpr uint64_t no_type = ~uint64_t(0);

		public:
			template<typename T> uint64_t id() {
				static uint64_t result = no_type;
				if (result != no_type)
					return result;
				result = runningTypeIndex++;
				return result;
			}
		};

		class entity_index {
		public:
			uint64_t generateOne() { return ++m_nextId; } // zero is never generated. we can use that as InvalidId.

		private:
			uint64_t m_nextId = 0;
			static constexpr uint64_t InvalidId = 0;
		};

		type_index types;
		entity_index entities;

	public:
		class table_index {
		public:
			static const uint64_t npos = ~uint64_t(0);

			enum class data_view {
				PassThrough,
				Invert
			};

			table_index& set(uint64_t id) {
				uint64_t block = id >> 6;
				uint8_t bit = id & 63;
				if (block >= m_index_data.size())
					m_index_data.resize(((3 * block) >> 1) + 1);
				m_index_data[block] |= uint64_t(1) << bit;
				return *this;
			}

			table_index& reset(uint64_t id) {
				uint64_t block = id >> 6;
				uint8_t bit = id & 63;
				m_index_data[block] &= ~(uint64_t(1) << bit);
				return *this;
			}

			template<data_view view>
			table_index& merge(const table_index& other) {
				size_t mySize = m_index_data.size();
				size_t otherSize = other.m_index_data.size();
				if (mySize > otherSize) {
					for (size_t i = 0; i < otherSize; ++i)
						if constexpr (view == data_view::PassThrough)
							m_index_data.data()[i] &= other.m_index_data[i];
						else if constexpr (view == data_view::Invert)
							m_index_data.data()[i] &= ~other.m_index_data[i];
					for (size_t i = otherSize; i < mySize; ++i)
						m_index_data.data()[i] = 0;
				}
				else {
					for (size_t i = 0; i < mySize; ++i)
						if constexpr (view == data_view::PassThrough)
							m_index_data.data()[i] &= other.m_index_data[i];
						else if constexpr (view == data_view::Invert)
							m_index_data.data()[i] &= ~other.m_index_data[i];
				}
				return *this;
			}

			uint64_t next(uint64_t firstAllowed = 0) {
				uint64_t block = firstAllowed >> 6;
				uint8_t bit = firstAllowed & 63;

				if (block >= m_index_data.size())
					return npos;

				uint64_t blockData = *(m_index_data.data() + block);
				blockData &= ~((uint64_t(1) << uint64_t(bit)) - uint64_t(1));

				if (blockData) {
					uint64_t firstSetBit = findFirstSetBit(blockData);
					return (block << 6) + firstSetBit;
				}

				while (++block < m_index_data.size()) {
					uint64_t blockData = m_index_data[block];
					if (blockData) {
						uint64_t firstSetBit = findFirstSetBit(blockData);
						return (block << 6) + firstSetBit;
					}
				}

				return npos;
			}

		private:
			std::vector<uint64_t> m_index_data;
		};

		template<typename T>
		class component_table {
		public:
			void insert(uint64_t id, T&& t) {
				if (id >= m_data.size())
					m_data.resize(((3 * id) >> 1) + 1);
				*(m_data.data() + id) = std::forward<T>(t);
				m_index.set(id);
			}

			void erase(uint64_t id) { m_index.reset(id); }
			table_index& index() { return m_index; }
			inline T& operator[](uint64_t id) { return *(m_data.data() + id); }

		private:
			std::vector<std::remove_reference_t<T>> m_data;
			table_index m_index;
		};

		template<typename F> class iterator {};
		template<typename Ret, typename Cls, typename... Args>
		class iterator<Ret(Cls::*)(Args...) const> {
		public:
			iterator(ecs& ecs) : m_ecs(ecs) {}

			template<typename F> void for_each(F&& op) {
				unpack_types<Args...>();

				// precalculate constraint results into a new index to avoid trashing cache when iterating
				table_index res = *indices[0];
				for (size_t i = 1; i < indices.size(); ++i)
					res.merge<table_index::data_view::PassThrough>(*indices[i]);
				for (auto* ind : not_in)
					res.merge<table_index::data_view::Invert>(*ind);

				auto tables = std::make_tuple(&m_ecs.table<Args>()...);

				// iterate and call op for each present index.
				uint64_t n = res.next(0);
				while (n != table_index::npos) {
					make_indexed_call(std::forward<F>(op), n, tables, std::index_sequence_for<Args...>());
					n = res.next(++n);
				}
			}

			template<typename... Ts> iterator& include(Ts&& ... ts) { unpack(std::forward<Ts>(ts)...); }
			template<typename... Ts> iterator& exclude(Ts&& ... ts) { unpack_exclude(std::forward<Ts>(ts)...); }

		private:
			template<typename Type, typename... Ts> void unpack_types_indirection() {
				unpack_types_single<Type>();
				unpack_types<Ts...>();
			}
			template<typename Type> void unpack_types_single() { indices.emplace_back(&m_ecs.table<typename std::remove_reference_t<Type>>().index()); }
			template<typename... Ts> void unpack_types() { unpack_types_indirection<Ts...>(); }
			template<> void unpack_types<>() {}

			template<typename F, typename... Ts, std::size_t... Is> void make_indexed_call(F&& op, size_t n, std::tuple<Ts...>& tuple, std::index_sequence<Is...>) { op(std::get<Is>(tuple)->operator[](n)...); }
			
			template<typename T, typename... Ts>
			void unpack(T&& t, Ts&& ... ts) {
				unpack(t);
				unpack(std::forward<Ts>(ts)...);
			}

			template<typename T, typename... Ts>
			void unpack_exclude(T&& t, Ts&& ... ts) {
				unpack_exclude(std::forward<T>(t));
				unpack_exclude(std::forward<Ts>(ts)...);
			}

			void unpack_exclude(table_index& index) { not_in.emplace_back(&index); }
			template<typename T> void unpack_exclude(component_table<T>& t) { not_in.emplace_back(&t.index()); }

			void unpack(table_index& index) { indices.emplace_back(&index); }
			template<typename T> void unpack(component_table<T>& t) { indices.emplace_back(&t.index()); }
			template<typename T> void unpack(T&& t) {} // if it's not a table, ignore.
			std::vector<table_index*> indices;
			std::vector<table_index*> not_in;
			ecs& m_ecs;
		};

		template<typename F>
		void for_each(F&& op) {
			iterator<decltype(&F::operator())> it(*this);
			it.for_each(std::forward<F>(op));
		}

		template<typename...Ts> void void_func(Ts&& ...) {}

		template<typename... Components>
		uint64_t create(Components&& ... components) {
			uint64_t id = entities.generateOne();
			void_func(addComponent(id, std::forward<Components>(components))...);
			return id;
		}

		template<typename... Components>
		ecs& attachToEntity(uint64_t id, Components&& ... components) {
			void_func(addComponent(id, std::forward<Components>(components))...);
			return *this;
		}

		template<typename... Components>
		ecs& removeFromEntity(uint64_t id) {
			void_func(removeComponent<Components>(id)...);
			return *this;
		}

	private:
		template<typename T>
		ecs& addComponent(uint64_t id, T&& component) {
			auto& componentTable = table<T>();
			componentTable.insert(id, std::forward<T>(component));
			return *this;
		}

		template<typename T>
		ecs& removeComponent(uint64_t id) {
			auto& componentTable = table<T>();
			componentTable.erase(id);
			return *this;
		}

		template<typename T, typename U = std::remove_const_t<std::remove_reference_t<T>>>
		component_table<U> & table() {
			uint64_t typeIndex = types.id<U>();
			if (typeIndex >= m_components.size())
				m_components.resize(((3 * typeIndex) >> 1) + 1, nullptr);
			if (m_components[typeIndex] == nullptr)
				m_components[typeIndex] = new component_table<U>();
			return *static_cast<component_table<U>*>(m_components[typeIndex]);
		}
	};

}