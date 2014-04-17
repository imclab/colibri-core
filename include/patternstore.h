#ifndef COLIBRIPATTERNSTORE_H
#define COLIBRIPATTERNSTORE_H

#include <string>
#include <iostream>
#include <ostream>
#include <istream>
#include <unordered_map>
#include <vector>
#include <set>
#include <map>
#include <array>
#include <unordered_set>
#include <iomanip> // contains setprecision()
#include <exception>
#include <algorithm>
#include "common.h"
#include "pattern.h"
#include "datatypes.h"
#include "classdecoder.h"

/***********************************************************************************/

//Class for reading an entire (class encoded) corpus into memory, providing a
//reverse index by IndexReference
class IndexedCorpus {
    protected:
        std::map<IndexReference,Pattern> data; //tokens
    public:
        IndexedCorpus() {};
        IndexedCorpus(std::istream *in);
        IndexedCorpus(std::string filename);
        
        void load(std::istream *in);
        void load(std::string filename);
        typedef std::map<IndexReference,Pattern>::iterator iterator;
        typedef std::map<IndexReference,Pattern>::const_iterator const_iterator;

        iterator begin() { return data.begin(); }
        const_iterator begin() const { return data.begin(); }

        iterator end() { return data.end(); }
        const_iterator end() const { return data.end(); }

        iterator find(const IndexReference ref) { return data.find(ref); }
        const_iterator find(const IndexReference ref) const { return data.find(ref); }
        
        bool has(const IndexReference ref) const { return data.count(ref); }

        size_t size() const { return data.size(); } 

        Pattern operator [](const IndexReference ref) { return data[ref]; } 

        Pattern getpattern(IndexReference begin, int length);
         
        std::vector<IndexReference> findmatches(const Pattern & pattern, int maxmatches=0); //by far not as efficient as a pattern model obviously

        int sentencelength(int sentence); 
};


/************* Base abstract container for pattern storage  ********************/

class PatternStoreInterface {
    public:
        virtual bool has(const Pattern &) const =0;
        virtual bool has(const PatternPointer &) const =0;
        virtual size_t size() const =0; 
};

//This is an abstract class, all Pattern storage containers are derived from
//this. ContainerType is the low-level container type used (an STL container such as set/map). ReadWriteSizeType influences only the maximum number of items that can be stored (2**64) in the container, as this will be represented in the very beginning of the binary file. No reason to change this unless the container is very deeply nested in others and contains only few items.
template<class ContainerType,class ReadWriteSizeType = uint64_t>
class PatternStore: public PatternStoreInterface {
    public:
        PatternStore<ContainerType,ReadWriteSizeType>() {};
        ~PatternStore<ContainerType,ReadWriteSizeType>() {};
    

        virtual void insert(const Pattern & pattern)=0; //might be a noop in some implementations that require a value

        virtual bool has(const Pattern &) const =0;
        virtual bool has(const PatternPointer &) const =0;
        virtual bool erase(const Pattern &) =0;
        
        virtual size_t size() const =0; 
        
        
        typedef typename ContainerType::iterator iterator;
        typedef typename ContainerType::const_iterator const_iterator;

        virtual typename ContainerType::iterator begin()=0;
        virtual typename ContainerType::iterator end()=0;
        virtual typename ContainerType::iterator find(const Pattern & pattern)=0;
        
        virtual void write(std::ostream * out)=0;
        //virtual void read(std::istream * in, int MINTOKENS)=0;

        virtual PatternStoreInterface * getstoreinterface() {
            return (PatternStoreInterface*) this;
        }
};


/************* Abstract datatype for all kinds of maps ********************/


template<class ContainerType, class ValueType, class ValueHandler,class ReadWriteSizeType = uint32_t>
class PatternMapStore: public PatternStore<ContainerType,ReadWriteSizeType> { 
     protected:
        ValueHandler valuehandler;
     public:
        PatternMapStore<ContainerType,ValueType,ValueHandler,ReadWriteSizeType>(): PatternStore<ContainerType,ReadWriteSizeType>() {};
        ~PatternMapStore<ContainerType,ValueType,ValueHandler,ReadWriteSizeType>() {};

        virtual void insert(const Pattern & pattern, ValueType & value)=0;

        virtual bool has(const Pattern &) const =0;
        virtual bool has(const PatternPointer &) const =0;
        virtual bool erase(const Pattern &) =0;
        
        virtual size_t size() const =0; 
        
        
        virtual ValueType & operator [](const Pattern & pattern)=0;
        virtual ValueType & operator [](const PatternPointer & pattern)=0;
        
        typedef typename ContainerType::iterator iterator;
        typedef typename ContainerType::const_iterator const_iterator;

        virtual typename ContainerType::iterator begin()=0;
        virtual typename ContainerType::iterator end()=0;
        virtual typename ContainerType::iterator find(const Pattern & pattern)=0;

        virtual void write(std::ostream * out) {
            ReadWriteSizeType s = (ReadWriteSizeType) size();
            out->write( (char*) &s, sizeof(ReadWriteSizeType));
            for (iterator iter = this->begin(); iter != this->end(); iter++) {
                Pattern p = iter->first;
                p.write(out);
                this->valuehandler.write(out, iter->second);
            }
        }
        
        virtual void write(std::string filename) {
            std::ofstream * out = new std::ofstream(filename.c_str());
            this->write(out);
            out->close();
            delete out;
        }


        template<class ReadValueType=ValueType, class ReadValueHandler=ValueHandler>
        void read(std::istream * in, int MINTOKENS=0, int MINLENGTH=0, int MAXLENGTH=999999, PatternStoreInterface * constrainstore = NULL, bool DONGRAMS=true, bool DOSKIPGRAMS=true, bool DOFLEXGRAMS=true, bool DORESET=false) {
            ReadValueHandler readvaluehandler = ReadValueHandler();
            ReadWriteSizeType s; //read size:
            in->read( (char*) &s, sizeof(ReadWriteSizeType));
            std::cerr << "Reading " << s << " patterns" << std::endl;
            //std::cerr << "Reading " << (int) s << " patterns" << std::endl;
            for (ReadWriteSizeType i = 0; i < s; i++) {
                Pattern p;
                try {
                    p = Pattern(in);
                } catch (std::exception &e) {
                    std::cerr << "ERROR: Exception occurred at pattern " << (i+1) << " of " << s << std::endl;
                    throw InternalError();
                }
                if (!DONGRAMS || !DOSKIPGRAMS || !DOFLEXGRAMS) {
                    const PatternCategory c = p.category();
                    if ((!DONGRAMS && c == NGRAM) || (!DOSKIPGRAMS && c == SKIPGRAM) || (!DOFLEXGRAMS && c == FLEXGRAM)) continue;
                }
                const int n = p.size();
                ReadValueType readvalue;
                //std::cerr << "Read pattern: " << std::endl;
                readvaluehandler.read(in, readvalue);
                if (n >= MINLENGTH && n <= MAXLENGTH)  {
                    if ((readvaluehandler.count(readvalue) >= MINTOKENS) && ((constrainstore == NULL) || (constrainstore->has(p)))) {
                            ValueType convertedvalue;
                            if (!DORESET) readvaluehandler.convertto(readvalue, convertedvalue); 
                            this->insert(p,convertedvalue);
                    }
                }
            }
        }

        void read(std::string filename,int MINTOKENS=0, int MINLENGTH=0, int MAXLENGTH=999999, PatternStoreInterface * constrainstore = NULL, bool DONGRAMS=true, bool DOSKIPGRAMS=true, bool DOFLEXGRAMS=true, bool DORESET = false) { //no templates for this one, easier on python/cython
            std::ifstream * in = new std::ifstream(filename.c_str());
            this->read<ValueType,ValueHandler>(in,MINTOKENS,MINLENGTH,MAXLENGTH,constrainstore,DONGRAMS,DOSKIPGRAMS,DOFLEXGRAMS, DORESET);
            in->close();
            delete in;
        }

};


/************* Specific STL-like containers for patterns ********************/


/*
 * These are the containers you can directly use from your software
 *
 */

typedef std::unordered_set<Pattern> t_patternset;

template<class ReadWriteSizeType = uint32_t>
class PatternSet: public PatternStore<t_patternset,ReadWriteSizeType> {
    protected:
        t_patternset data;
    public:

        PatternSet<ReadWriteSizeType>(): PatternStore<t_patternset,ReadWriteSizeType>() {};
        ~PatternSet<ReadWriteSizeType>() {};

        void insert(const Pattern & pattern) {
            data.insert(pattern);
        }

        bool has(const Pattern & pattern) const { return data.count(pattern); }
        bool has(const PatternPointer & pattern) const { return data.count(Pattern(pattern)); }
        size_t size() const { return data.size(); } 


        typedef t_patternset::iterator iterator;
        typedef t_patternset::const_iterator const_iterator;
        
        iterator begin() { return data.begin(); }
        const_iterator begin() const { return data.begin(); }

        iterator end() { return data.end(); }
        const_iterator end() const { return data.end(); }

        iterator find(const Pattern & pattern) { return data.find(pattern); }
        const_iterator find(const Pattern & pattern) const { return data.find(pattern); }

        bool erase(const Pattern & pattern) { return data.erase(pattern); }
        iterator erase(const_iterator position) { return data.erase(position); }


        void write(std::ostream * out) {
            ReadWriteSizeType s = (ReadWriteSizeType) size();
            out->write( (char*) &s, sizeof(ReadWriteSizeType));
            for (iterator iter = begin(); iter != end(); iter++) {
                Pattern p = *iter;
                p.write(out);
            }
        }

        void read(std::istream * in, int MINLENGTH=0, int MAXLENGTH=999999, PatternStoreInterface * constrainstore = NULL, bool DONGRAMS=true, bool DOSKIPGRAMS=true, bool DOFLEXGRAMS=true) {
            ReadWriteSizeType s; //read size:
            in->read( (char*) &s, sizeof(ReadWriteSizeType));
            for (unsigned int i = 0; i < s; i++) {
                Pattern p = Pattern(in);
                if (!DONGRAMS || !DOSKIPGRAMS || !DOFLEXGRAMS) {
                    const PatternCategory c = p.category();
                    if ((!DONGRAMS && c == NGRAM) || (!DOSKIPGRAMS && c == SKIPGRAM) || (!DOFLEXGRAMS && c == FLEXGRAM)) continue;
                }
                const int n = p.size();
                if ((n >= MINLENGTH && n <= MAXLENGTH) && ((constrainstore == NULL) || (constrainstore->has(p)))) {
                    insert(p);
                }
            }
        }

        template<class ReadValueType, class ReadValueHandler=BaseValueHandler<ReadValueType>>
        void readmap(std::istream * in, int MINTOKENS=0, int MINLENGTH=0, int MAXLENGTH=999999, PatternStoreInterface * constrainstore = NULL, bool DONGRAMS=true, bool DOSKIPGRAMS=true, bool DOFLEXGRAMS=true) {
            ReadValueHandler readvaluehandler = ReadValueHandler();
            ReadWriteSizeType s; //read size:
            in->read( (char*) &s, sizeof(ReadWriteSizeType));
            //std::cerr << "Reading " << (int) s << " patterns" << std::endl;
            for (ReadWriteSizeType i = 0; i < s; i++) {
                Pattern p;
                try {
                    p = Pattern(in);
                } catch (std::exception &e) {
                    std::cerr << "ERROR: Exception occurred at pattern " << (i+1) << " of " << s << std::endl;
                    throw InternalError();
                }
                if (!DONGRAMS || !DOSKIPGRAMS || !DOFLEXGRAMS) {
                    const PatternCategory c = p.category();
                    if ((!DONGRAMS && c == NGRAM) || (!DOSKIPGRAMS && c == SKIPGRAM) || (!DOFLEXGRAMS && c == FLEXGRAM)) continue;
                }
                const int n = p.size();
                ReadValueType readvalue;
                //std::cerr << "Read pattern: " << std::endl;
                readvaluehandler.read(in, readvalue);
                if (n >= MINLENGTH && n <= MAXLENGTH)  {
                    if ((readvaluehandler.count(readvalue) >= MINTOKENS) && ((constrainstore == NULL) || (constrainstore->has(p)))) {
                        this->insert(p);
                    }
                }
            }
        }
};


typedef std::set<Pattern> t_orderedpatternset;


template<class ReadWriteSizeType = uint64_t>
class OrderedPatternSet: public PatternStore<t_orderedpatternset,ReadWriteSizeType> {
    protected:
        t_orderedpatternset data;
    public:

        OrderedPatternSet<ReadWriteSizeType>(): PatternStore<t_orderedpatternset,ReadWriteSizeType>() {};
        ~OrderedPatternSet<ReadWriteSizeType>();

        void insert(const Pattern pattern) {
            data.insert(pattern);
        }

        bool has(const Pattern & pattern) const { return data.count(pattern); }
        bool has(const PatternPointer & pattern) const { return data.count(Pattern(pattern)); }
        size_t size() const { return data.size(); } 

        typedef t_orderedpatternset::iterator iterator;
        typedef t_orderedpatternset::const_iterator const_iterator;
        
        iterator begin() { return data.begin(); }
        const_iterator begin() const { return data.begin(); }

        iterator end() { return data.end(); }
        const_iterator end() const { return data.end(); }

        iterator find(const Pattern & pattern) { return data.find(pattern); }
        const_iterator find(const Pattern & pattern) const { return data.find(pattern); }

        bool erase(const Pattern & pattern) { return data.erase(pattern); }
        iterator erase(const_iterator position) { return data.erase(position); }

        
        void write(std::ostream * out) {
            ReadWriteSizeType s = (ReadWriteSizeType) size();
            out->write( (char*) &s, sizeof(ReadWriteSizeType));
            for (iterator iter = begin(); iter != end(); iter++) {
                Pattern p = *iter;
                p.write(out);
            }
        }

        void read(std::istream * in, int MINLENGTH=0, int MAXLENGTH=999999, bool DONGRAMS=true, bool DOSKIPGRAMS=true, bool DOFLEXGRAMS=true) {
            ReadWriteSizeType s; //read size:
            in->read( (char*) &s, sizeof(ReadWriteSizeType));
            for (unsigned int i = 0; i < s; i++) {
                Pattern p = Pattern(in);
                if (!DONGRAMS || !DOSKIPGRAMS || !DOFLEXGRAMS) {
                    const PatternCategory c = p.category();
                    if ((!DONGRAMS && c == NGRAM) || (!DOSKIPGRAMS && c == SKIPGRAM) || (!DOFLEXGRAMS && c == FLEXGRAM)) continue;
                }
                const int n = p.size();
                if (n >= MINLENGTH && n <= MAXLENGTH)  {
                    insert(p);
                }
            }
        }

};


template<class ValueType, class ValueHandler = BaseValueHandler<ValueType>, class ReadWriteSizeType = uint64_t>
class PatternMap: public PatternMapStore<std::unordered_map<const Pattern,ValueType>,ValueType,ValueHandler,ReadWriteSizeType> {
    protected:
        std::unordered_map<const Pattern, ValueType> data;
    public:
        //PatternMap(): PatternMapStore<std::unordered_map<const Pattern, ValueType>,ValueType,ValueHandler,ReadWriteSizeType>() {};
        PatternMap<ValueType,ValueHandler,ReadWriteSizeType>() {};

        void insert(const Pattern & pattern, ValueType & value) { 
            data[pattern] = value;
        }

        void insert(const Pattern & pattern) {  data[pattern] = ValueType(); } //singular insert required by PatternStore, implies 'default' ValueType, usually 0
        
        bool has(const Pattern & pattern) const { return data.count(pattern); }
        bool has(const PatternPointer & pattern) const { return data.count(Pattern(pattern)); }

        size_t size() const { return data.size(); } 

        ValueType& operator [](const Pattern & pattern) { return data[pattern]; } 
        ValueType& operator [](const PatternPointer & pattern) { return data[Pattern(pattern)]; } 
        
        typedef typename std::unordered_map<const Pattern,ValueType>::iterator iterator;
        typedef typename std::unordered_map<const Pattern,ValueType>::const_iterator const_iterator;
        
        iterator begin() { return data.begin(); }
        const_iterator begin() const { return data.begin(); }

        iterator end() { return data.end(); }
        const_iterator end() const { return data.end(); }

        iterator find(const Pattern & pattern) { return data.find(pattern); }
        const_iterator find(const Pattern & pattern) const { return data.find(pattern); }
        
        bool erase(const Pattern & pattern) { return data.erase(pattern); }
        iterator erase(const_iterator position) { return data.erase(position); }

};


template<class ValueType,class ValueHandler = BaseValueHandler<ValueType>,class ReadWriteSizeType = uint64_t>
class OrderedPatternMap: public PatternMapStore<std::map<const Pattern,ValueType>,ValueType,ValueHandler,ReadWriteSizeType> {
    protected:
        std::map<const Pattern, ValueType> data;
    public:
        OrderedPatternMap<ValueType,ValueHandler,ReadWriteSizeType>(): PatternMapStore<std::map<const Pattern, ValueType>,ValueType,ValueHandler,ReadWriteSizeType>() {};
        ~OrderedPatternMap<ValueType,ValueHandler,ReadWriteSizeType>() {};

        void insert(const Pattern & pattern, ValueType & value) { 
            data[pattern] = value;
        }

        void insert(const Pattern & pattern) {  data[pattern] = ValueType(); } //singular insert required by PatternStore, implies 'default' ValueType

        bool has(const Pattern & pattern) const { return data.count(pattern); }
        bool has(const PatternPointer & pattern) const { return data.count(Pattern(pattern)); }

        size_t size() const { return data.size(); } 

        ValueType& operator [](const Pattern & pattern) { return data[pattern]; } 
        ValueType& operator [](const PatternPointer & pattern) { return data[Pattern(pattern)]; } 
        
        typedef typename std::map<const Pattern,ValueType>::iterator iterator;
        typedef typename std::map<const Pattern,ValueType>::const_iterator const_iterator;
        
        iterator begin() { return data.begin(); }
        const_iterator begin() const { return data.begin(); }

        iterator end() { return data.end(); }
        const_iterator end() const { return data.end(); }

        iterator find(const Pattern & pattern) { return data.find(pattern); }
        const_iterator find(const Pattern & pattern) const { return data.find(pattern); }

        bool erase(const Pattern & pattern) { return data.erase(pattern); }
        iterator erase(const_iterator position) { return data.erase(position); }
        

};


template<class T,size_t N,int countindex = 0>
class ArrayValueHandler: public AbstractValueHandler<T> {
   public:
    const static bool indexed = false;
    virtual std::string id() { return "ArrayValueHandler"; }
    void read(std::istream * in, std::array<T,N> & a) {
        for (int i = 0; i < N; i++) {
            T v;
            in->read( (char*) &v, sizeof(T)); 
            a[i] = v;
        }
    }
    void write(std::ostream * out, std::array<T,N> & a) {
        for (int i = 0; i < N; i++) {
            T v = a[i];
            out->write( (char*) &v, sizeof(T)); 
        }
    }
    std::string tostring(std::array<T,N> & a) {
        std::string s;
        for (int i = 0; i < N; i++) {
            T v = a[i];
            if (!s.empty()) s += " ";
            s += " " + tostring(a[i]);
        }
        return s;
    }
    int count(std::array<T,N> & a) const {
        return (int) a[countindex];
    }
    void add(std::array<T,N> * value, const IndexReference & ref ) const {
        (*value)[countindex] += 1;
    }
};


//using a PatternStore as value in a PatternMap requires a special valuehandler:
template<class PatternStoreType>
class PatternStoreValueHandler: public AbstractValueHandler<PatternStoreType> {
  public:
    const static bool indexed = false;
    virtual std::string id() { return "PatternStoreValueHandler"; }
    void read(std::istream * in,  PatternStoreType & value) {
        value.read(in);
    }
    void write(std::ostream * out,  PatternStoreType & value) {
        value.write(out);
    }
    virtual std::string tostring(  PatternStoreType & value) {
        std::cerr << "PatternStoreValueHandler::tostring() is not supported" << std::endl;
        throw InternalError();
    }
    int count( PatternStoreType & value) const {
        return value.size();
    }
    void add( PatternStoreType * value, const IndexReference & ref ) const {
        std::cerr << "PatternStoreValueHandler::add() is not supported" << std::endl;
        throw InternalError();
    }
};

template<class ValueType,class ValueHandler=BaseValueHandler<ValueType>, class NestedSizeType = uint16_t >
class AlignedPatternMap: public PatternMap< PatternMap<ValueType,ValueHandler,NestedSizeType>,PatternStoreValueHandler<PatternMap<ValueType,ValueHandler,NestedSizeType>>, uint64_t > {
    public:
        typedef PatternMap<ValueType,ValueHandler,NestedSizeType> valuetype;
        typedef typename PatternMap< PatternMap<ValueType,ValueHandler,NestedSizeType>,PatternStoreValueHandler<PatternMap<ValueType,ValueHandler,NestedSizeType>>, uint64_t >::iterator iterator;
        typedef typename PatternMap< PatternMap<ValueType,ValueHandler,NestedSizeType>,PatternStoreValueHandler<PatternMap<ValueType,ValueHandler,NestedSizeType>>, uint64_t >::const_iterator const_iterator;

};


//TODO: Implement a real Trie, conserving more memory
#endif
