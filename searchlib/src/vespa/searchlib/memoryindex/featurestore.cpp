// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <vespa/fastos/fastos.h>
#include <vespa/log/log.h>
LOG_SETUP(".memoryindex.featurestore");
#include "featurestore.h"
#include <vespa/searchlib/index/schemautil.h>
#include <vespa/searchlib/datastore/datastore.hpp>

namespace search
{

namespace memoryindex
{

constexpr size_t MIN_CLUSTERS = 1024u;

using index::SchemaUtil;

uint64_t
FeatureStore::writeFeatures(uint32_t packedIndex,
                            const DocIdAndFeatures &features)
{
    _f._fieldsParams = &_fieldsParams[packedIndex];
    uint64_t oldOffset = _f.getWriteOffset();
    assert((oldOffset & 63) == 0);
    if (oldOffset > 2000) {
        _f.setupWrite(_fctx);
        oldOffset = 0;
        assert(_f.getWriteOffset() == oldOffset);
    }
    assert(!features.getRaw());
    _f.writeFeatures(features);
    return oldOffset;
}


datastore::EntryRef
FeatureStore::addFeatures(const uint8_t *src, uint64_t byteLen)
{
    uint32_t pad = RefType::pad(byteLen);
    auto result = _store.rawAllocator<uint8_t>(_typeId).alloc(byteLen + pad, DECODE_SAFETY);
    uint8_t *dst = result.data;
    memcpy(dst, src, byteLen);
    dst += byteLen;
    if (pad > 0) {
        memset(dst, 0, pad);
        dst += pad;
    }
    memset(dst, 0, DECODE_SAFETY);
    return result.ref;
}


std::pair<datastore::EntryRef, uint64_t>
FeatureStore::addFeatures(uint64_t beginOffset, uint64_t endOffset)
{
    uint64_t bitLen = (endOffset - beginOffset);
    assert(static_cast<int64_t>(bitLen) > 0);
    uint64_t wordLen = (bitLen + 63) / 64;
    uint64_t byteLen = (bitLen + 7) / 8;
    assert(wordLen > 0);
    assert(byteLen > 0);
    const uint8_t *src = reinterpret_cast<const uint8_t *>(_f._valI - wordLen);
    RefType ref = addFeatures(src, byteLen);
    return std::make_pair(ref, bitLen);
}


datastore::EntryRef
FeatureStore::moveFeatures(datastore::EntryRef ref, uint64_t bitLen)
{
    const uint8_t *src = getBits(ref);
    uint64_t byteLen = (bitLen + 7) / 8;
    RefType newRef = addFeatures(src, byteLen);
    // Mark old features as dead
    _store.incDead(ref, byteLen + RefType::pad(byteLen));
    return newRef;
}


FeatureStore::FeatureStore(const Schema &schema)
    : _store(),
      _f(NULL),
      _fctx(_f),
      _d(NULL),
      _fieldsParams(),
      _schema(schema),
      _type(RefType::align(1u), MIN_CLUSTERS,
            RefType::offsetSize() / RefType::align(1u)),
      _typeId(0)
{
    _f.setWriteContext(&_fctx);
    _fctx.allocComprBuf(64, 1);
    _f.afterWrite(_fctx, 0, 0);

    _fieldsParams.resize(_schema.getNumIndexFields());
    SchemaUtil::IndexIterator it(_schema);
    for(; it.isValid(); ++it) {
        _fieldsParams[it.getIndex()].
            setSchemaParams(_schema, it.getIndex());
    }
    _store.addType(&_type);
    _store.initActiveBuffers();
}


FeatureStore::~FeatureStore(void)
{
    _store.dropBuffers();
}


std::pair<datastore::EntryRef, uint64_t>
FeatureStore::addFeatures(uint32_t packedIndex,
                          const DocIdAndFeatures &features)
{
    uint64_t oldOffset = writeFeatures(packedIndex, features);
    uint64_t newOffset = _f.getWriteOffset();
    _f.flush();
    return addFeatures(oldOffset, newOffset);
}



void
FeatureStore::getFeatures(uint32_t packedIndex, datastore::EntryRef ref,
                          DocIdAndFeatures &features)
{
    setupForField(packedIndex, _d);
    setupForReadFeatures(ref, _d);
    _d.readFeatures(features);
}


size_t
FeatureStore::bitSize(uint32_t packedIndex, datastore::EntryRef ref)
{
    setupForField(packedIndex, _d);
    setupForUnpackFeatures(ref, _d);
    uint64_t oldOffset = _d.getReadOffset();
    _d.skipFeatures(1);
    uint64_t newOffset = _d.getReadOffset();
    uint64_t bitLen = (newOffset - oldOffset);
    assert(static_cast<int64_t>(bitLen) > 0);
    return bitLen;
}


datastore::EntryRef
FeatureStore::moveFeatures(uint32_t packedIndex,
                           datastore::EntryRef ref)
{
    uint64_t bitLen = bitSize(packedIndex, ref);
    return moveFeatures(ref, bitLen);
}


} // namespace memoryindex


} // namespace search
