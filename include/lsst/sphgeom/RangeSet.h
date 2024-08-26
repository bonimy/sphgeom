/*
 * This file is part of sphgeom.
 *
 * Developed for the LSST Data Management System.
 * This product includes software developed by the LSST Project
 * (http://www.lsst.org).
 * See the COPYRIGHT file at the top-level directory of this distribution
 * for details of code ownership.
 *
 * This software is dual licensed under the GNU General Public License and also
 * under a 3-clause BSD license. Recipients may choose which of these licenses
 * to use; please see the files gpl-3.0.txt and/or bsd_license.txt,
 * respectively.  If you choose the GPL option then the following text applies
 * (but note that there is still no warranty even if you opt for BSD instead):
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LSST_SPHGEOM_RANGESET_H_
#define LSST_SPHGEOM_RANGESET_H_

/// \file
/// \brief This file provides a type for representing integer sets.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iosfwd>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <vector>


namespace lsst {
namespace sphgeom {

/// A `RangeSet` is a set of unsigned 64 bit integers.
///
/// Internally, elements in the set are tracked using a sorted vector of
/// disjoint, non-empty, half-open ranges, which is memory efficient
/// for sets containing many consecutive integers.
///
/// Given a hierarchical pixelization of the sphere and a simple spherical
/// region, a RangeSet is a good way to store the indexes of pixels
/// intersecting the region. For an in-depth discussion of this use case, see:
///
/// > Efficient data structures for masks on 2D grids
/// > M. Reinecke and E. Hivon
/// > Astronomy & Astrophysics, Volume 580, id.A132, 9 pp.
/// >
/// > http://www.aanda.org/articles/aa/abs/2015/08/aa26549-15/aa26549-15.html
///
/// The beginning and end points of the disjoint, non-empty, half-open integer
/// ranges in the set are stored in a std::vector<std::uint64_t>, with monotonically
/// increasing values, except for the last one. Each pair of consecutive
/// elements [begin, end) in the vector is a non-empty half-open range, where
/// the value of end is defined as the integer obtained by adding one to the
/// largest element in the range.
///
/// Mathematically, a half-open range with largest element equal to 2^64 - 1
/// would have an end point of 2^64. But arithmetic for unsigned 64 bit
/// integers is modular, and adding 1 to 2^64 - 1 "wraps around" to 0. So in
/// practice, ranges containing the largest std::uint64_t value have an end point
/// of 0. Note that overflow is undefined for signed integers, which motivates
/// the use of unsigned 64 bit integers for this class.
///
/// The first and last values of the internal vector are are always 0, even if
/// no range in the set has a beginning or end point of 0. To illustrate why,
/// consider the contents of the vector for a set containing a single
/// integer, 3:
///
///     [0, 3, 4, 0]
///
/// The range obtained by extracting pairs of elements from the vector
/// starting at index 1 is [3, 4), which corresponds to the contents of the
/// set. The ranges obtained by starting at index 0 are [0, 3) and [4, 0).
/// They correspond to the unsigned 64 bit integers not in the set.
///
/// The use of bookended half open ranges means that simply toggling the index
/// of the first range between 0 and 1 corresponds to complementing the set.
/// This allows many simplifications in the implementation - for example,
/// set union and difference can be implemented in terms of set intersection
/// and complement, since A ∪ B = ¬(¬A ∩ ¬B) and A ∖ B = A ∩ ¬B.
///
/// Many of the RangeSet methods accept ranges of integers [first, last) as
/// input, either directly or in the form of a tuple. The values in a range
/// are generated by starting with a value equal to first, and incrementing
/// it until last is reached. If first == last, the range is full (it contains
/// all possible std::uint64_t values), and if first > last, it wraps around -
/// that is, it contains all std::uint64_t values except for [last, first).
///
/// The ranges in a set can be iterated over. Set modification may
/// invalidate all iterators.
class RangeSet {
public:

    /// A constant iterator over the ranges (represented as 2-tuples) in a
    /// RangeSet.
    ///
    /// RangeSet does not store an array of 2-tuples internally. But,
    /// §24.2.5 of the C++11 standard requires the following from
    /// constant forward iterators:
    ///
    /// - (1.3) The `reference` type of the iterator must be a reference to
    ///   `const value_type`
    /// - (6) If a and b are both dereferenceable, then a == b if and only if
    ///   *a and *b are bound to the same object.
    ///
    /// So, dereferencing a forward iterator cannot return by value, or return
    /// a reference to a member of the iterator itself. As a result, it seems
    /// impossible to provide more than an input iterator for container
    /// objects that do not store their values directly.
    ///
    /// While this class only claims to be an input iterator, it nevertheless
    /// implements most random access iterator requirements. Dereferencing
    /// an iterator returns a tuple by value, and `operator->` is omitted.
    struct Iterator {
        // For std::iterator_traits
        using difference_type = ptrdiff_t;
        using value_type = std::tuple<std::uint64_t, std::uint64_t>;
        using pointer = void;
        using reference = std::tuple<std::uint64_t, std::uint64_t>;
        using iterator_category = std::input_iterator_tag;

        Iterator() = default;
        explicit Iterator(std::uint64_t const * ptr) : p{ptr} {}

        friend void swap(Iterator & a, Iterator & b) {
            std::swap(a.p, b.p);
        }

        // Arithmetic
        Iterator & operator++() { p += 2; return *this; }
        Iterator & operator--() { p -= 2; return *this; }

        Iterator operator++(int) { Iterator i(*this); p += 2; return i; }
        Iterator operator--(int) { Iterator i(*this); p -= 2; return i; }

        Iterator operator+(ptrdiff_t n) const { return Iterator(p + 2 * n); }
        Iterator operator-(ptrdiff_t n) const { return Iterator(p + 2 * n); }

        Iterator & operator+=(ptrdiff_t n) { p += 2 * n; return *this; }
        Iterator & operator-=(ptrdiff_t n) { p -= 2 * n; return *this; }

        friend Iterator operator+(ptrdiff_t n, Iterator const & i) {
            return i + n;
        }

        ptrdiff_t operator-(Iterator const & i) const { return (p - i.p) / 2; }

        // Comparison
        bool operator==(Iterator const & i) const { return p == i.p; }
        bool operator!=(Iterator const & i) const { return p != i.p; }
        bool operator<(Iterator const & i) const { return p < i.p; }
        bool operator>(Iterator const & i) const { return p > i.p; }
        bool operator<=(Iterator const & i) const { return p <= i.p; }
        bool operator>=(Iterator const & i) const { return p >= i.p; }

        std::tuple<std::uint64_t, std::uint64_t> operator*() {
            return std::make_tuple(p[0], p[1]);
        }

        std::tuple<std::uint64_t, std::uint64_t> operator[](ptrdiff_t n) const {
            return std::make_tuple(p[2 * n], p[2 * n + 1]);
        }

        std::uint64_t const * p = nullptr;
    };

    using difference_type = ptrdiff_t;
    using size_type = size_t;
    using value_type = std::tuple<std::uint64_t, std::uint64_t>;
    using const_iterator = Iterator;

    RangeSet(RangeSet const &) = default;
    RangeSet(RangeSet &&) = default;
    RangeSet & operator=(RangeSet const &) = default;
    RangeSet & operator=(RangeSet &&) = default;

    /// The default constructor creates an empty set.
    RangeSet() = default;

    ///@{
    /// This constructor creates a set containing the given integer(s)
    /// or integer range(s).
    explicit RangeSet(std::uint64_t u) {
        insert(u);
    }

    RangeSet(std::uint64_t first, std::uint64_t last) {
        insert(first, last);
    }

    template <
        typename U,
        typename = typename std::enable_if<
            std::is_convertible<U, std::uint64_t>::value
        >::type
    >
    explicit RangeSet(std::tuple<U, U> const & range) {
        insert(static_cast<std::uint64_t>(std::get<0>(range)),
               static_cast<std::uint64_t>(std::get<1>(range)));
    }

    RangeSet(std::initializer_list<std::uint64_t>);

    // Note: the standard library for GCC unfortunately declares explicit conversion
    // constructors for tuples - the use of std::pair is required for code like:
    //
    //      RangeSet s = {{0, 1}, {2, 3}};
    //
    // to compile.
    RangeSet(std::initializer_list<std::pair<std::uint64_t, std::uint64_t>>);
    ///@}

    /// This generic constructor creates a set containing the integers or
    /// ranges obtained by dereferencing the iterators in [a, b).
    ///
    /// It is hidden via SFINAE if InputIterator is not a standard
    /// input iterator.
    template <
        typename InputIterator,
        typename = typename std::enable_if<
            std::is_base_of<
                std::input_iterator_tag,
                typename std::iterator_traits<InputIterator>::iterator_category
            >::value
        >::type
    >
    RangeSet(InputIterator a, InputIterator b) {
        for (; a != b; ++a) {
            insert(*a);
        }
    }

    /// This generic constructor creates a set containing the integers or
    /// integer ranges in the given container.
    ///
    /// It is hidden via SFINAE if Container does not provide constant input
    /// iterators over its elements.
    template <
        typename Container,
        typename = typename std::enable_if<
            std::is_base_of<
                std::input_iterator_tag,
                typename std::iterator_traits<decltype(
                    std::begin(std::declval<typename std::add_const<Container>::type>())
                )>::iterator_category
            >::value
            &&
            std::is_base_of<
                std::input_iterator_tag,
                typename std::iterator_traits<decltype(
                    std::end(std::declval<typename std::add_const<Container>::type>())
                )>::iterator_category
            >::value
        >::type
    >
    explicit RangeSet(Container const & c) :
        RangeSet(std::begin(c), std::end(c)) {}

    ///@{
    /// Two RangeSet instances are equal iff they contain the same integers.
    bool operator==(RangeSet const & rs) const {
        return _offset == rs._offset && _ranges == rs._ranges;
    }

    bool operator!=(RangeSet const & rs) const {
        return _offset != rs._offset || _ranges != rs._ranges;
    }
    ///@}

    ///@{
    /// `insert` adds the given integer(s) to this set.
    ///
    /// It is strongly exception safe, and runs in amortized constant time if
    /// the given integers extend or follow the last (largest) range in this
    /// set. Otherwise, the worst case run time is O(N), where N is the number
    /// of ranges in the set.
    void insert(std::uint64_t u) {
        insert(u, u + 1);
    }

    template <
        typename U,
        typename = typename std::enable_if<
            std::is_convertible<U, std::uint64_t>::value
        >::type
    >
    void insert(std::tuple<U, U> const & range) {
        insert(std::get<0>(range), std::get<1>(range));
    }

    void insert(std::uint64_t first, std::uint64_t last);
    ///@}

    ///@{
    /// `erase` removes the given integers from this set.
    ///
    /// The strong exception safety guarantee is provided.
    void erase(std::uint64_t u) {
        erase(u, u + 1);
    }

    template <
        typename U,
        typename = typename std::enable_if<
            std::is_convertible<U, std::uint64_t>::value
        >::type
    >
    void erase(std::tuple<U, U> const & range) {
        erase(std::get<0>(range), std::get<1>(range));
    }

    void erase(std::uint64_t first, std::uint64_t last);
    ///@}

    /// \name Set operations
    ///@{

    /// `complement` replaces this set S with U ∖ S, where U is the
    /// universe of range sets, [0, 2^64). It runs in constant time.
    RangeSet & complement() { _offset = !_offset; return *this; }

    /// `complemented` returns a complemented copy of this set.
    RangeSet complemented() const {
        RangeSet s(*this);
        s.complement();
        return s;
    }

    /// `intersection` returns the intersection of this set and s.
    RangeSet intersection(RangeSet const & s) const;

    /// `join` returns the union of this set and s.
    RangeSet join(RangeSet const & s) const;

    /// `difference` returns the difference between this set and s.
    RangeSet difference(RangeSet const & s) const;

    /// `symmetricDifference` returns the symmetric difference of
    /// this set and s.
    RangeSet symmetricDifference(RangeSet const & s) const;

    /// The ~ operator returns the complement of this set.
    RangeSet operator~() const {
        RangeSet s(*this);
        s.complement();
        return s;
    }

    /// The & operator returns the intersection of this set and s.
    RangeSet operator&(RangeSet const & s) const {
        return intersection(s);
    }

    /// The | operator returns the union of this set and s.
    RangeSet operator|(RangeSet const & s) const {
        return join(s);
    }

    /// The - operator returns the difference between this set and s.
    RangeSet operator-(RangeSet const & s) const {
        return difference(s);
    }

    /// The ^ operator returns the symmetric difference between this set and s.
    RangeSet operator^(RangeSet const & s) const {
        return symmetricDifference(s);
    }

    /// The &= operator assigns the intersection of this set and s to this set.
    /// It is strongly exception safe.
    RangeSet & operator&=(RangeSet const & s) {
        if (this != &s) {
            RangeSet r = intersection(s);
            swap(r);
        }
        return *this;
    }

    /// The |= operator assigns the union of this set and s to this set.
    /// It is strongly exception safe.
    RangeSet & operator|=(RangeSet const & s) {
        if (this != &s) {
            RangeSet r = join(s);
            swap(r);
        }
        return *this;
    }

    /// The -= operator assigns the difference between this set and s
    /// to this set. It is strongly exception safe.
    RangeSet & operator-=(RangeSet const & s) {
        if (this != &s) {
            RangeSet r = difference(s);
            swap(r);
        } else {
            clear();
        }
        return *this;
    }

    /// The ^= operator assigns the symmetric difference between this set
    /// and s to this set. It is strongly exception safe.
    RangeSet & operator^=(RangeSet const & s) {
        if (this != &s) {
            RangeSet r = symmetricDifference(s);
            swap(r);
        } else {
            clear();
        }
        return *this;
    }
    ///@}

    ///@{
    /// `intersects` returns true iff the intersection of this set
    /// and the given integers is non-empty.
    bool intersects(std::uint64_t u) const { return intersects(u, u + 1); }

    bool intersects(std::uint64_t first, std::uint64_t last) const;

    bool intersects(RangeSet const & s) const;
    ///@}

    ///@{
    /// `contains` returns true iff every one of the given integers is in
    /// this set.
    bool contains(std::uint64_t u) const { return contains(u, u + 1); }

    bool contains(std::uint64_t first, std::uint64_t last) const;

    bool contains(RangeSet const & s) const;
    ///@}

    ///@{
    /// `isWithin` returns true iff every integer in this set is also one of
    /// the given integers.
    bool isWithin(std::uint64_t u) const { return isWithin(u, u + 1); }

    bool isWithin(std::uint64_t first, std::uint64_t last) const;

    bool isWithin(RangeSet const & s) const { return s.contains(*this); }
    ///@}

    ///@{
    /// `isDisjointFrom` returns true iff the intersection of this set
    /// and the given integers is empty.
    bool isDisjointFrom(std::uint64_t u) const { return !intersects(u); }

    bool isDisjointFrom(std::uint64_t first, std::uint64_t last) const {
        return !intersects(first, last);
    }

    bool isDisjointFrom(RangeSet const & s) const { return !intersects(s); }
    ///@}

    /// `simplify` simplifies this range set by "coarsening" its ranges.
    ///
    /// The result is defined as the union of the ranges obtained by
    /// rounding existing range beginnings down to the nearest multiple of
    /// 2^n, and rounding the ends up. Therefore, simplifying a range
    /// set always results in a superset of the original set.
    ///
    /// This function replaces many small ranges with fewer coarser ranges.
    /// If the ranges correspond to pixels in a hierarchical pixelization
    /// of the sphere that overlap a region R, then this operation can be
    /// thought of as computing a lower resolution representation of the
    /// coverage of R.
    RangeSet & simplify(std::uint32_t n);

    /// `simplified` returns a simplified copy of this set.
    RangeSet simplified(std::uint32_t n) const {
        RangeSet rs(*this);
        rs.simplify(n);
        return rs;
    }

    /// `scale` multiplies the endpoints of each range in this set by the
    /// given integer.
    ///
    /// Given ranges that correspond to pixel indexes in a hierarchical
    /// pixelization of the sphere like HTM or Q3C, scaling by 4 corresponds
    /// to increasing the subdivision level of the pixelization by 1.
    RangeSet & scale(std::uint64_t i);

    /// `scaled` returns a scaled copy of this set.
    RangeSet scaled(std::uint64_t i) const {
        RangeSet rs(*this);
        rs.scale(i);
        return rs;
    }

    /// `clear` removes all integers from this set.
    void clear() { _ranges = {0, 0}; _offset = true; }

    /// `fill` adds all the unsigned 64 bit integers to this set.
    void fill() { _ranges = {0, 0}; _offset = false; }

    /// `empty` checks whether there are any integers in this set.
    bool empty() const { return _begin() == _end(); }

    /// `full` checks whether all integers in the universe of range sets,
    /// [0, 2^64), are in this set.
    bool full() const { return _beginc() == _endc(); }

    ///@{
    /// This function returns a constant iterator to the first range
    /// in this set.
    Iterator begin() const { return Iterator(_begin()); }
    Iterator cbegin() const { return begin(); }
    ///@}

    ///@{
    /// This function returns a constant iterator to the range after
    /// the last one in this set.
    Iterator end() const { return Iterator(_end()); }
    Iterator cend() const { return end(); }
    ///@}

    /// `beginc` returns a constant iterator to the first range in the
    /// complement of this set.
    Iterator beginc() const { return Iterator(_beginc()); }

    /// `endc` returns a constant iterator to to the range after the last
    /// one in the complement of this set.
    Iterator endc() const { return Iterator(_endc()); }

    /// `max_size` returns the maximum number of ranges a set can hold.
    size_t max_size() const { return _ranges.max_size() / 2; }

    /// `size` returns the number of ranges in this set.
    size_t size() const { return (_ranges.size() - _offset) / 2; }

    /// `cardinality` returns the number of integers in this set.
    ///
    /// Note that 0 is returned both for full and empty sets (a full set
    /// contains 2^64 integers, which is 0 modulo 2^64).
    std::uint64_t cardinality() const;

    void swap(RangeSet & s) {
        using std::swap;
        swap(_ranges, s._ranges);
        swap(_offset, s._offset);
    }

    /// `isValid` checks that this RangeSet is in a valid state.
    ///
    /// It is intended for use by unit tests, but calling it in other contexts
    /// is harmless. A return value of false means the RangeSet implementation
    /// isn't preserving its invariants, i.e. has a bug.
    bool isValid() const;

private:
    std::vector<std::uint64_t> _ranges = {0, 0};

    // The offset of the first range in _ranges. It is 0 (false) if the
    // first integer in the set is 0, and 1 (true) otherwise.
    bool _offset = true;

    // `_begin` returns a pointer to the first range in this set.
    std::uint64_t const * _begin() const { return _ranges.data() + _offset; }

    // `_end` returns a pointer to the range after the last one in this set.
    std::uint64_t const * _end() const {
        size_t s = _ranges.size();
        return _ranges.data() + (s - ((s & 1) ^ _offset));
    }

    // `_beginc` returns a pointer to the first range in
    // the complement of this set.
    std::uint64_t const * _beginc() const { return _ranges.data() + !_offset; }

    // `_endc` returns a pointer to the range after the last one in
    // the complement of this set.
    std::uint64_t const * _endc() const {
        size_t s = _ranges.size();
        return _ranges.data() + (s - ((s & 1) ^ !_offset));
    }

    void _insert(std::uint64_t first, std::uint64_t last);

    static void _intersectOne(std::vector<std::uint64_t> &,
                              std::uint64_t const *,
                              std::uint64_t const *, std::uint64_t const *);

    static void _intersect(std::vector<std::uint64_t> &,
                           std::uint64_t const *, std::uint64_t const *,
                           std::uint64_t const *, std::uint64_t const *);

    void _intersect(std::uint64_t const *, std::uint64_t const *,
                    std::uint64_t const *, std::uint64_t const *);

    static bool _intersectsOne(std::uint64_t const *,
                               std::uint64_t const *, std::uint64_t const *);

    static bool _intersects(std::uint64_t const *, std::uint64_t const *,
                            std::uint64_t const *, std::uint64_t const *);
};


inline void swap(RangeSet & a, RangeSet & b) {
    a.swap(b);
}

std::ostream & operator<<(std::ostream &, RangeSet const &);

}} // namespace lsst::sphgeom

#endif // LSST_SPHGEOM_RANGESET_H_
