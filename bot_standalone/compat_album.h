#ifndef COMPAT_ALBUM_H_
#define COMPAT_ALBUM_H_

#include "compat_types.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#define TeakLibW_Exception(a, b, c, d)

template <typename T> class BUFFER_V : public std::vector<T> {
  public:
    BUFFER_V() = default;
    BUFFER_V(SLONG size) : std::vector<T>(size) {}
    void ReSize(SLONG anz) {
        Offset = 0;
        std::vector<T>::resize(anz);
    }
    SLONG AnzEntries() const { return std::vector<T>::size(); }
    void Clear() {
        Offset = 0;
        std::vector<T>::clear();
    }
    void FillWith(T value) {
        for (SLONG i = 0; i < std::vector<T>::size(); i++)
            std::vector<T>::at(i) = value;
    }

    // operator T *() const { return std::vector<T>::data(); }
    const T *getData() const { return std::vector<T>::data() + Offset; }
    T *getData() { return std::vector<T>::data() + Offset; }

    // void operator+=(SLONG rhs) { DelPointer += rhs; }
    void incIter(SLONG i) { Offset += i; }
    SLONG getIter() const { return Offset; }

#ifdef DEBUG_ALBUM
    T &operator[](size_t pos) { return std::vector<T>::at(pos); }
    const T &operator[](size_t pos) const { return std::vector<T>::at(pos); };
#endif

    // keep using function names of old BUFFER class for compatibility, but
    // make sure that std::vector<> is not directly accessed
    void clear() { Clear(); }
    void resize(std::size_t count) { ReSize(count); }
    void resize(std::size_t count, const T &value) {
        ReSize(count);
        FillWith(value);
    }

  private:
    SLONG Offset{0};
};

template <typename T> class ALBUM_V {
  public:
    /* album iter */

    using element_type = std::pair<T, ULONG>;
    class Iter {
      public:
        using difference_type = SLONG;
        using value_type = T;
        using pointer = T *;
        using reference = T &;
        using iterator_category = std::random_access_iterator_tag;

        Iter(typename std::vector<element_type>::iterator it, std::unordered_map<ULONG, SLONG> *h) : It(it), Hash(h) {}
        inline Iter &operator++() {
            It++;
            return (*this);
        }
        inline Iter &operator--() {
            It--;
            return (*this);
        }
        inline Iter &operator+=(SLONG i) {
            It += i;
            return (*this);
        }
        inline Iter &operator-=(SLONG i) {
            It -= i;
            return (*this);
        }
        friend Iter operator+(Iter it, SLONG i) { return Iter(it.It + i, it.Hash); }
        friend Iter operator-(Iter it, SLONG i) { return Iter(it.It - i, it.Hash); }
        friend Iter operator+(SLONG i, Iter it) { return Iter(it.It + i, it.Hash); }
        friend Iter operator-(SLONG i, Iter it) { return Iter(it.It - i, it.Hash); }
        inline difference_type operator-(Iter it) const { return It - it.It; }
        inline reference operator*() const { return It->first; }
        inline bool operator==(const Iter &i) const { return It == i.It; }
        inline bool operator!=(const Iter &i) const { return It != i.It; }

        inline bool operator<(const Iter &i) const { return It < i.It; }
        inline bool operator<=(const Iter &i) const { return It <= i.It; }
        inline bool operator>(const Iter &i) const { return It > i.It; }
        inline bool operator>=(const Iter &i) const { return It >= i.It; }

        static void swap(Iter &a, Iter &b) {
            if (a == b) {
                return;
            }
            auto *h = a.Hash;
            if (h->end() != h->find(a.It->second)) {
                h->at(a.It->second) += (b.It - a.It);
            }
            if (h->end() != h->find(b.It->second)) {
                h->at(b.It->second) -= (b.It - a.It);
            }
            std::iter_swap(a.It, b.It);
        }

        bool IsInAlbum() const { return It->second != 0; }

      private:
        typename std::vector<element_type>::iterator It;
        std::unordered_map<ULONG, SLONG> *Hash;
    };
    Iter begin() { return Iter(List.begin(), &Hash); }
    Iter end() { return Iter(List.end(), &Hash); }

    /* constructor */

    ALBUM_V(CString str) : Name(str) {}

    /* query capacity and resize */

    SLONG AnzEntries() const { return List.size(); }
    SLONG GetNumFree() const {
        return std::count_if(List.begin(), List.end(), [](const element_type &i) { return 0 == i.second; });
    }
    SLONG GetNumUsed() const { return AnzEntries() - GetNumFree(); }

    void ReSize(SLONG anz) {
        for (auto i = anz; i < AnzEntries(); i++) {
            Hash.erase(List[i].second);
            List[i].second = 0;
        }
        List.resize(anz);
        IdxBack = AnzEntries() - 1;
    }

    void ClearAlbum() {
        for (auto &i : List) {
            i.second = 0;
        }
        Hash = {};
        IdxFront = 0;
        IdxBack = AnzEntries() - 1;
    }

    void FillAlbum() {
        for (auto i = 0; i < AnzEntries(); i++) {
            if (List[i].second == 0) {
                ULONG id = GetUniqueId();
                List[i].second = id;
                Hash[id] = i;
            }
        }
        IdxFront = AnzEntries();
        IdxBack = -1;
    }

    /* accessing elements */

    ULONG GetIdFromIndex(SLONG i) const { return List[i].second; }

    SLONG IsInAlbum(ULONG id) const {
        if (id >= 0x1000000) {
            return (Hash.end() != Hash.find(id));
        }
        if (id < AnzEntries() && (List[id].second != 0)) {
            return 1;
        }
        return 0;
    }

    SLONG operator()(ULONG id) const { return find(id); }
    SLONG find(ULONG id) const {
        if (id >= 0x1000000) {
            auto it = Hash.find(id);
            if (it != Hash.end()) {
                return it->second;
            }
        } else if (id < AnzEntries()) {
            return id;
        }
        TeakLibW_Exception(nullptr, 0, ExcAlbumFind, Name.c_str());
        return 0;
    }

#ifdef DEBUG_ALBUM
    T &operator[](ULONG id) { return List.at(find(id)).first; }
    const T &operator[](ULONG id) const { return List.at(find(id)).first; }
#else
    T &operator[](ULONG id) { return List[find(id)].first; }
    const T &operator[](ULONG id) const { return List[find(id)].first; }
#endif
    T &at(ULONG id) { return List.at(find(id)).first; }
    const T &at(ULONG id) const { return List.at(find(id)).first; }

    /* comparison */

    bool operator==(const ALBUM_V<T> &l) const { return (LastId == l.LastId) && (Name == l.Name) && (List == l.List) && (Hash == l.Hash); }
    bool operator!=(const ALBUM_V<T> &l) const { return !operator==(l); }

    bool operator==(const std::vector<T> &l) const {
        if (AnzEntries() != l.size()) {
            return false;
        }
        for (SLONG i = 0; i < AnzEntries(); ++i) {
            if (List[i].first != l[i]) {
                return false;
            }
        }
        return true;
    }
    bool operator!=(const std::vector<T> &l) const { return !operator==(l); }

    /* modifiers */

    void ResetNextId() { LastId = 0xFFFFFF; }

    ULONG push_front(ULONG id, T rhs) {
        if (id >= 0x1000000 && (Hash.end() == Hash.find(id))) {
            for (SLONG i = IdxFront; i < AnzEntries(); ++i) {
                if (List[i].second == 0) {
                    std::swap(List[i].first, rhs);
                    List[i].second = id;
                    Hash[id] = i;
                    IdxFront = i + 1;
                    return id;
                }
            }
        }
        TeakLibW_Exception(nullptr, 0, ExcAlbumInsert, Name.c_str());
        return 0;
    }

    ULONG push_back(ULONG id, T rhs) {
        if (id >= 0x1000000 && (Hash.end() == Hash.find(id))) {
            for (SLONG i = IdxBack; i >= 0; --i) {
                if (List[i].second == 0) {
                    std::swap(List[i].first, rhs);
                    List[i].second = id;
                    Hash[id] = i;
                    IdxBack = i - 1;
                    return id;
                }
            }
        }
        TeakLibW_Exception(nullptr, 0, ExcAlbumInsert, Name.c_str());
        return 0;
    }

    ULONG operator*=(T rhs) {
        auto id = GetUniqueId();
        return push_front(id, std::move(rhs));
    }

    ULONG operator+=(T rhs) {
        auto id = GetUniqueId();
        return push_back(id, std::move(rhs));
    }

    void operator-=(ULONG id) {
        if (id >= 0x1000000) {
            auto it = Hash.find(id);
            if (it != Hash.end()) {
                List[it->second].second = 0;
                IdxFront = std::min(IdxFront, it->second);
                IdxBack = std::max(IdxBack, it->second);
            }
            Hash.erase(id);
            return;
        }
        SLONG idx = id;
        if (idx < AnzEntries() && (List[idx].second != 0)) {
            Hash.erase(List[idx].second);
            List[idx].second = 0;
            IdxFront = std::min(IdxFront, idx);
            IdxBack = std::max(IdxBack, idx);
            return;
        }
        TeakLibW_Exception(nullptr, 0, ExcAlbumDelete, Name.c_str());
    }

    void Sort() {
        IdxFront = 0;
        IdxBack = AnzEntries() - 1;

        auto a = List.begin();
        auto b = List.end() - 1;
        while (true) {
            while ((a->second != 0) && a < b) {
                ++a;
            }
            while ((b->second == 0) && a < b) {
                --b;
            }
            if (a >= b) {
                break;
            }
            std::iter_swap(a, b);
        }
#ifdef DEBUG_ALBUM
        assert(a == b);
#endif
        if (a->second != 0) {
            ++a;
        }
        std::stable_sort(List.begin(), a);
        rebuild_hash_table();
    }

    void Swap(SLONG a, SLONG b) {
        if (a == b) {
            return;
        }
        IdxFront = 0;
        IdxBack = AnzEntries() - 1;
        if (a >= 0x1000000 && b >= 0x1000000) {
            auto idxA = find(a);
            auto idxB = find(b);
            Hash.at(a) = idxB;
            Hash.at(b) = idxA;
            std::swap(List[idxA], List[idxB]);
            return;
        }
        if (a < 0x1000000 && b < 0x1000000) {
            auto idA = List[a].second;
            auto idB = List[b].second;
            if (Hash.end() != Hash.find(idA)) {
                Hash.at(idA) = b;
            }
            if (Hash.end() != Hash.find(idB)) {
                Hash.at(idB) = a;
            }
            std::swap(List[a], List[b]);
            return;
        }
        TeakLibW_Exception(nullptr, 0, ExcAlbumInvalidArg, Name.c_str());
    }

    void check_consistent_index() {
        for (auto i = 0; i < AnzEntries(); i++) {
            auto id = GetIdFromIndex(i);
            if ((id != 0) && find(id) != i) {
                TeakLibW_Exception(nullptr, 0, ExcAlbumNotConsistent, Name.c_str());
            }
        }
    }

  private:
    void rebuild_hash_table() {
        Hash.clear();
        for (SLONG i = 0; i < AnzEntries(); ++i) {
            auto id = List[i].second;
            if (id >= 0x1000000) {
                Hash[id] = i;
            }
        }
    }

  private:
    SLONG GetUniqueId() { return ++LastId; }

    ULONG LastId{0xFFFFFF};
    SLONG IdxFront{};
    SLONG IdxBack{};
    std::vector<element_type> List;
    std::unordered_map<ULONG, SLONG> Hash;
    CString Name;
};

#endif // COMPAT_ALBUM_H_
