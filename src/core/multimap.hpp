/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file multimap.hpp Multimap with deterministic ordering of items with equal keys. */

#ifndef MULTIMAP_HPP_
#define MULTIMAP_HPP_

#include <map>
#include <list>

template<typename Tkey, typename Tvalue, typename Tcompare>
class MultiMap;

template<class Tmap_iter, class Tlist_iter, class Tkey, class Tvalue, class Tcompare>
class MultiMapIterator {
protected:
	friend class MultiMap<Tkey, Tvalue, Tcompare>;
	typedef MultiMapIterator<Tmap_iter, Tlist_iter, Tkey, Tvalue, Tcompare> Self;
	Tlist_iter list_iter;
	Tmap_iter map_iter;
	bool list_valid;

public:
	MultiMapIterator() : list_valid(false) {}
	template<class Tnon_const>
	MultiMapIterator(Tnon_const mi) : map_iter(mi), list_valid(false) {}
	MultiMapIterator(Tmap_iter mi, Tlist_iter li) : list_iter(li), map_iter(mi)
	{
		this->list_valid = (this->list_iter != this->map_iter->second.begin());
	}

	template<class Tnon_const>
	Self &operator=(Tnon_const mi)
	{
		this->map_iter = mi;
		this->list_valid = false;
	}

	Tvalue &operator*() const
	{
		assert(!this->map_iter->second.empty());
		if (this->list_valid) {
			return this->list_iter.operator*();
		} else {
			return this->map_iter->second.begin().operator*();
		}
	}

	Tvalue *operator->() const
	{
		assert(!this->map_iter->second.empty());
		if (this->list_valid) {
			return this->list_iter.operator->();
		} else {
			return this->map_iter->second.begin().operator->();
		}
	}

	inline const Tmap_iter &GetMapIter() const {return this->map_iter;}
	inline const Tlist_iter &GetListIter() const {return this->list_iter;}
	inline bool ListValid() const {return this->list_valid;}

	const Tkey &GetKey() const {return this->map_iter->first;}

	Self &operator++()
	{
		assert(!this->map_iter->second.empty());
		if (this->list_valid) {
			if(++this->list_iter == this->map_iter->second.end()) {
				++this->map_iter;
				this->list_valid = false;
			}
		} else {
			this->list_iter = ++(this->map_iter->second.begin());
			if (this->list_iter == this->map_iter->second.end()) {
				++this->map_iter;
			} else {
				this->list_valid = true;
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
		assert(!this->map_iter->second.empty());
		if (!this->list_valid) {
			--this->map_iter;
			this->list_iter = this->map_iter->second.end();
			assert(!this->map_iter->second.empty());
		}

		this->list_valid = (--this->list_iter != this->map_iter->second.begin());
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

template<class Tmap_iter1, class Tlist_iter1, class Tmap_iter2, class Tlist_iter2, class Tkey, class Tvalue1, class Tvalue2, class Tcompare>
bool operator==(const MultiMapIterator<Tmap_iter1, Tlist_iter1, Tkey, Tvalue1, Tcompare> &iter1, const MultiMapIterator<Tmap_iter2, Tlist_iter2, Tkey, Tvalue2, Tcompare> &iter2)
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

template<class Tmap_iter1, class Tlist_iter1, class Tmap_iter2, class Tlist_iter2, class Tkey, class Tvalue1, class Tvalue2, class Tcompare>
bool operator!=(const MultiMapIterator<Tmap_iter1, Tlist_iter1, Tkey, Tvalue1, Tcompare> &iter1, const MultiMapIterator<Tmap_iter2, Tlist_iter2, Tkey, Tvalue2, Tcompare> &iter2)
{
	return !(iter1 == iter2);
}

template<class Tmap_iter1, class Tlist_iter1, class Tmap_iter2, class Tkey, class Tvalue, class Tcompare >
bool operator==(const MultiMapIterator<Tmap_iter1, Tlist_iter1, Tkey, Tvalue, Tcompare> &iter1, const Tmap_iter2 &iter2)
{
	return !iter1.ListValid() && iter1.GetMapIter() == iter2;
}

template<class Tmap_iter1, class Tlist_iter1, class Tmap_iter2, class Tkey, class Tvalue, class Tcompare >
bool operator!=(const MultiMapIterator<Tmap_iter1, Tlist_iter1, Tkey, Tvalue, Tcompare> &iter1, const Tmap_iter2 &iter2)
{
	return iter1.ListValid() || iter1.GetMapIter() != iter2;
}

template<class Tmap_iter1, class Tlist_iter1, class Tmap_iter2, class Tkey, class Tvalue, class Tcompare >
bool operator==(const Tmap_iter2 &iter2, const MultiMapIterator<Tmap_iter1, Tlist_iter1, Tkey, Tvalue, Tcompare> &iter1)
{
	return !iter1.ListValid() && iter1.GetMapIter() == iter2;
}

template<class Tmap_iter1, class Tlist_iter1, class Tmap_iter2, class Tkey, class Tvalue, class Tcompare >
bool operator!=(const Tmap_iter2 &iter2, const MultiMapIterator<Tmap_iter1, Tlist_iter1, Tkey, Tvalue, Tcompare> &iter1)
{
	return iter1.ListValid() || iter1.GetMapIter() != iter2;
}


/**
 * Hand-rolled multimap as map of lists. behaves mostly like a list, but is sorted
 * by Tkey.
 */
template<typename Tkey, typename Tvalue, typename Tcompare = std::less<Tkey> >
class MultiMap : public std::map<Tkey, std::list<Tvalue>, Tcompare > {
public:
	typedef typename std::list<Tvalue> List;
	typedef typename List::iterator ListIterator;
	typedef typename List::const_iterator ConstListIterator;

	typedef typename std::map<Tkey, List, Tcompare > Map;
	typedef typename Map::iterator MapIterator;
	typedef typename Map::const_iterator ConstMapIterator;

	typedef MultiMapIterator<MapIterator, ListIterator, Tkey, Tvalue, Tcompare> iterator;
	typedef MultiMapIterator<ConstMapIterator, ConstListIterator, Tkey, const Tvalue, Tcompare> const_iterator;

	void erase(iterator it)
	{
		List &list = it.map_iter->second;
		assert(!list.empty());
		if (it.ListValid()) {
			list.erase(it.list_iter);
		} else {
			list.erase(list.begin());
		}

		if (list.empty()) this->Map::erase(it.map_iter);
	}

	void Insert(const Tkey &key, const Tvalue &val)
	{
		List &list = (*this)[key];
		list.push_back(val);
		assert(!list.empty());
	}

	size_t size() const
	{
		size_t ret = 0;
		for (ConstMapIterator it = this->Map::begin(); it != this->Map::end(); ++it) {
			ret += it->second.size();
		}
		return ret;
	}

	size_t MapSize() const
	{
		return this->Map::size();
	}

	std::pair<iterator, iterator> equal_range(const Tkey &key)
	{
		MapIterator begin(lower_bound(key));
		if (begin != this->Map::end() && begin->first == key) {
			MapIterator end = begin;
			return std::make_pair(begin, ++end);
		} else {
			return std::make_pair(begin, begin);
		}
	}

	std::pair<const_iterator, const_iterator> equal_range(const Tkey &key) const
	{
		ConstMapIterator begin(lower_bound(key));
		if (begin != this->Map::end() && begin->first == key) {
			ConstMapIterator end = begin;
			return std::make_pair(begin, ++end);
		} else {
			return std::make_pair(begin, begin);
		}
	}
};

#endif /* MULTIMAP_HPP_ */
