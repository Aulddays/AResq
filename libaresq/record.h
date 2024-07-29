#pragma once

#include <stdint.h>
#include "audbg.h"

inline uint32_t p2l32(const uint8_t *pdata)
{
	return (uint32_t)pdata[0] |
		((uint32_t)pdata[1] << 8) |
		((uint32_t)pdata[2] << 16) |
		((uint32_t)pdata[3] << 24);
}

inline void l2p32(uint32_t ldata, uint8_t *pdata){
	pdata[0] = (uint8_t)ldata;
	pdata[1] = (uint8_t)(ldata >> 8);
	pdata[2] = (uint8_t)(ldata >> 16);
	pdata[3] = (uint8_t)(ldata >> 24);
}

inline uint32_t p2l24(const uint8_t *pdata)
{
	return (uint32_t)pdata[0] |
		((uint32_t)pdata[1] << 8) |
		((uint32_t)pdata[2] << 16);
}

inline void l2p24(uint32_t ldata, uint8_t *pdata){
	pdata[0] = (uint8_t)ldata;
	pdata[1] = (uint8_t)(ldata >> 8);
	pdata[2] = (uint8_t)(ldata >> 16);
}

inline uint32_t p2l16(const uint8_t *pdata)
{
	return (uint32_t)pdata[0] |
		((uint32_t)pdata[1] << 8);
}

inline void l2p16(uint32_t ldata, uint8_t *pdata){
	pdata[0] = (uint8_t)ldata;
	pdata[1] = (uint8_t)(ldata >> 8);
}

#pragma pack(push, 1)

class RecordItem
{
private:
	enum
	{
		RINAME = 0,
		RITIME = 4,
		RINEXT = 8,
		/* RIPARENT = RINEXT, */
		RISUB = 11,
		RISIZE = RISUB,
		RIFLAG = 14,
		RIRESERVD = 15,
		RIRECORDSIZE = 16
	};
	// name: 4; time: 4; union{next: 3, parent: 3}; union{dir(sub: 3), file(size: 3)}, flag: 1, reserved: 1
	// if islast(), ie, last item in dir, then `next` points back to parent dir
	uint8_t _data[RIRECORDSIZE]/* = { 0 }*/;

public:
	// uint32(_data + RINAME)
	inline uint32_t name() const { return p2l32(_data + RINAME); }
	inline void name(uint32_t name) { l2p32(name, _data + RINAME); }
	inline const char *name(const char *base) { return base + name(); }
	inline const char *name(const std::vector<char> &base) const { return base.data() + name(); }

	// uint32(_data + RITIME)
	inline uint32_t time() const { return p2l32(_data + RITIME); }
	inline void time(uint32_t time) { l2p32(time, _data + RITIME); }

	// uint24(_data + RINEXT/RIPARENT)
	inline uint32_t next() const { return p2l24(_data + RINEXT); }
	inline void next(uint32_t nid) { AuVerify(nid < (1 << 24)); l2p24(nid, _data + RINEXT); }
	//inline uint32_t parent() const { return p2l24(_data + RIPARENT); }
	//inline void parent(uint32_t nid) { AuVerify(nid < (1 << 24)); l2p24(nid, _data + RIPARENT); }

	// for file item (size)
	// uint24(_data + RISIZE)
	inline uint32_t size24() const { return p2l24(_data + RISIZE); }
	inline void size24(uint64_t size) { l2p24((uint32_t)size, _data + RISIZE); }
	inline bool sizeChanged(uint64_t size) { return (size & 0xffffff) != size24(); }

	// for dir item (sub)
	// uint24(_data + RISUB)
	inline uint32_t sub() const { return p2l24(_data + RISUB); }
	inline void sub(uint32_t subid) { AuVerify(subid < (1 << 24)); l2p24(subid, _data + RISUB); }

	// flags uint8(_data + RIFLAG): isdir, isactive, isslink, isexe, ispending, islast
private:
	inline bool getflag(int bit) const { return (_data[RIFLAG] & (1 << bit)) != 0; }
	inline void setflag(bool flag, int bit) { if (flag) _data[RIFLAG] |= (1u << bit); else _data[RIFLAG] &= ~(1u << bit); }
	enum { DIRBIT=0, ACTIVEBIT=1, IGNOREBIT=2, SLINKBIT=3, EXEBIT=4, PENDINGBIT=5, LASTBIT=6 };
public:
	inline bool isdir() const { return getflag(DIRBIT); }
	inline void isdir(bool flag) { setflag(flag, DIRBIT); }
	// isacitve: this record is not recycled
	inline bool isactive() const { return getflag(ACTIVEBIT); }
	inline void isactive(bool flag) { setflag(flag, ACTIVEBIT); }
	inline bool isignore() const { return getflag(IGNOREBIT); }
	inline void isignore(bool flag) { setflag(flag, IGNOREBIT); }
	inline bool isslink() const { return getflag(SLINKBIT); }
	inline void isslink(bool flag) { setflag(flag, SLINKBIT); }
	inline bool isexe() const { return getflag(EXEBIT); }
	inline void isexe(bool flag) { setflag(flag, EXEBIT); }
	// ispending: FILE: local change not synced
	inline bool ispending() const { return getflag(PENDINGBIT); }
	inline void ispending(bool flag) { setflag(flag, PENDINGBIT); }
	// islast: last item in dir, whose `next` points back to parent
	inline bool islast() const { return getflag(LASTBIT); }
	inline void islast(bool flag) { setflag(flag, LASTBIT); }

	inline uint8_t gettype() const { return _data[RIFLAG] & (1u << DIRBIT | 1u << SLINKBIT); }

public:
	RecordItem()
	{
		clear();
	}
	void clear()
	{
		for (int i = 0; i < sizeof(_data); ++i)
			_data[i] = 0;
	}
};

class HistItem
{
public:
	HistItem()
	{
		for (int i = 0; i < sizeof(_data); ++i)
			_data[i] = 0;
	}

	inline uint32_t next() const { return 0; };

private:
	uint8_t _data[4];
};

#pragma pack(pop)