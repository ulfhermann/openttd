/*
 * multimap.hpp
 *
 *  Created on: Sep 15, 2009
 *      Author: alve
 */

#ifndef MULTIMAP_HPP_
#define MULTIMAP_HPP_

#include <map>
#include <list>

template<typename KEY, typename VALUE, typename COMPARE>
class MultiMap;

template<class MAP_ITER, class LIST_ITER, class KEY, class VALUE, class COMPARE>
class MultiMapIterator {
protected:
	friend class MultiMap<KEY, VALUE, COMPARE>;
	typedef MultiMapIterator<MAP_ITER, LIST_ITER, KEY, VALUE, COMPARE> Self;
	LIST_ITER list_iter;
	MAP_ITER map_iter;
	bool list_valid;

public:
	MultiMapIterator() : list_valid(false) {}
	template<class NONCONST>
	MultiMapIterator(NONCONST mi) : map_iter(mi), list_valid(false) {}
	MultiMapIterator(MAP_ITER mi, LIST_ITER li) : list_iter(li), map_iter(mi)
	{
		list_valid = (list_iter != map_iter->second.begin());
	}

	template<class NONCONST>
	Self &operator=(NONCONST mi)
	{
		map_iter = mi;
		list_valid = false;
	}

	VALUE &operator*() const
	{
		assert(!map_iter->second.empty());
		if (list_valid) {
			return list_iter.operator*();
		} else {
			return map_iter->second.begin().operator*();
		}
	}

	VALUE *operator->() const
	{
		assert(!map_iter->second.empty());
		if (list_valid) {
			return list_iter.operator->();
		} else {
			return map_iter->second.begin().operator->();
		}
	}

	FORCEINLINE const MAP_ITER &GetMapIter() const {return map_iter;}
	FORCEINLINE const LIST_ITER &GetListIter() const {return list_iter;}
	FORCEINLINE bool ListValid() const {return list_valid;}

	const KEY &GetKey() const {return map_iter->first;}

	Self &operator++()
	{
		assert(!map_iter->second.empty());
		if (list_valid) {
			if(++list_iter == map_iter->second.end()) {
				++map_iter;
				list_valid = false;
			}
		} else {
			list_iter = ++(map_iter->second.begin());
			if (list_iter == map_iter->second.end()) {
				++map_iter;
			} else {
				list_valid = true;
			}
		}
		return *this;
	}

	Self operator++(int)
	{
		Self tmp = *this;
		this->operator++();
		return tmp;
	}

	Self &operator--()
	{
		assert(!map_iter->second.empty());
		if (!list_valid) {
			--map_iter;
			list_iter = map_iter->second.end();
			assert(!map_iter->second.empty());
		}

		if(--list_iter == map_iter->second.begin()) {
			list_valid = false;
		} else {
			list_valid = true;
		}

		return *this;
	}

	Self operator--(int)
	{
		Self tmp = *this;
		this->operator--();
		return tmp;
	}
};

/* generic comparison functions for const/non-const multimap iterators and map iterators */

template<class MAP_ITER1, class LIST_ITER1, class MAP_ITER2, class LIST_ITER2, class KEY, class VALUE1, class VALUE2, class COMPARE>
bool operator==(const MultiMapIterator<MAP_ITER1, LIST_ITER1, KEY, VALUE1, COMPARE> &iter1, const MultiMapIterator<MAP_ITER2, LIST_ITER2, KEY, VALUE2, COMPARE> &iter2)
{
	if (iter1.ListValid()) {
		if (!iter2.ListValid()) {
			return false;
		} else if (iter1.GetListIter() != iter2.GetListIter()) {
			return false;
		}
	} else if (iter2.ListValid()) {
		return false;
	}
	return (iter1.GetMapIter() == iter2.GetMapIter());
}

template<class MAP_ITER1, class LIST_ITER1, class MAP_ITER2, class LIST_ITER2, class KEY, class VALUE1, class VALUE2, class COMPARE>
bool operator!=(const MultiMapIterator<MAP_ITER1, LIST_ITER1, KEY, VALUE1, COMPARE> &iter1, const MultiMapIterator<MAP_ITER2, LIST_ITER2, KEY, VALUE2, COMPARE> &iter2)
{
	return !(iter1 == iter2);
}

template<class MAP_ITER1, class LIST_ITER1, class MAP_ITER2, class KEY, class VALUE, class COMPARE >
bool operator==(const MultiMapIterator<MAP_ITER1, LIST_ITER1, KEY, VALUE, COMPARE> &iter1, const MAP_ITER2 &iter2)
{
	return !iter1.ListValid() && iter1.GetMapIter() == iter2;
}

template<class MAP_ITER1, class LIST_ITER1, class MAP_ITER2, class KEY, class VALUE, class COMPARE >
bool operator!=(const MultiMapIterator<MAP_ITER1, LIST_ITER1, KEY, VALUE, COMPARE> &iter1, const MAP_ITER2 &iter2)
{
	return iter1.ListValid() || iter1.GetMapIter() != iter2;
}

template<class MAP_ITER1, class LIST_ITER1, class MAP_ITER2, class KEY, class VALUE, class COMPARE >
bool operator==(const MAP_ITER2 &iter2, const MultiMapIterator<MAP_ITER1, LIST_ITER1, KEY, VALUE, COMPARE> &iter1)
{
	return !iter1.ListValid() && iter1.GetMapIter() == iter2;
}

template<class MAP_ITER1, class LIST_ITER1, class MAP_ITER2, class KEY, class VALUE, class COMPARE >
bool operator!=(const MAP_ITER2 &iter2, const MultiMapIterator<MAP_ITER1, LIST_ITER1, KEY, VALUE, COMPARE> &iter1)
{
	return iter1.ListValid() || iter1.GetMapIter() != iter2;
}


/**
 * hand-rolled multimap as map of lists. behaves mostly like a list, but is sorted
 * by KEY.
 */
template<typename KEY, typename VALUE, typename COMPARE = std::less<KEY> >
class MultiMap : public std::map<KEY, std::list<VALUE>, COMPARE > {
public:
	typedef typename std::list<VALUE> List;
	typedef typename List::iterator ListIterator;
	typedef typename List::const_iterator ConstListIterator;

	typedef typename std::map<KEY, List, COMPARE > Map;
	typedef typename Map::iterator MapIterator;
	typedef typename Map::const_iterator ConstMapIterator;

	typedef MultiMapIterator<MapIterator, ListIterator, KEY, VALUE, COMPARE> iterator;
	typedef MultiMapIterator<ConstMapIterator, ConstListIterator, KEY, const VALUE, COMPARE> const_iterator;

	void erase(iterator it)
	{
		List &list = it.map_iter->second;
		assert(!list.empty());
		if (it.ListValid()) {
			list.erase(it.list_iter);
		} else {
			list.erase(list.begin());
		}

		if (list.empty()) {
			Map::erase(it.map_iter);
		}
	}

	void Insert(const KEY &key, const VALUE &val)
	{
		List &list = (*this)[key];
		list.push_back(val);
		assert(!list.empty());
	}

	size_t size() const
	{
		size_t ret = 0;
		for(ConstMapIterator it = Map::begin(); it != Map::end(); ++it) {
			ret += it->second.size();
		}
		return ret;
	}

	size_t MapSize() const
	{
		return Map::size();
	}

	std::pair<iterator, iterator> equal_range(const KEY &key)
	{
		MapIterator begin(lower_bound(key));
		if (begin != Map::end() && begin->first == key) {
			MapIterator end = begin;
			return std::make_pair(begin, ++end);
		} else {
			return std::make_pair(begin, begin);
		}
	}

	std::pair<const_iterator, const_iterator> equal_range(const KEY &key) const
	{
		ConstMapIterator begin(lower_bound(key));
		if (begin != Map::end() && begin->first == key) {
			ConstMapIterator end = begin;
			return std::make_pair(begin, ++end);
		} else {
			return std::make_pair(begin, begin);
		}
	}
};

#endif /* MULTIMAP_HPP_ */
