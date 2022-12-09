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
public:
	// uint32(_data)
	inline uint32_t name() const { return p2l32(_data); }
	inline void name(uint32_t name) { l2p32(name, _data); }
	inline const char *name(const char *base) { return base + name(); }
	inline const char *name(const std::vector<char> &base) const { return base.data() + name(); }

	// uint32(_data + 4)
	inline uint32_t time() const { return p2l32(_data + 4); }
	inline void time(uint32_t time) { l2p32(time, _data + 4); }

	// uint24(_data + 8)
	inline uint32_t next() const { return p2l24(_data + 8); }
	inline void next(uint32_t nid) { AuVerify(nid < (1 << 24)); l2p24(nid, _data + 8); }

	// for file item (hist, size)
	// uint24(_data + 11)
	inline uint32_t hist() const { return p2l24(_data + 11); }
	inline void hist(uint32_t histid) { AuVerify(histid < (1 << 24)); l2p24(histid, _data + 11); }

	// uint24(_data + 14)
	inline uint32_t size24() const { return p2l24(_data + 14); }
	inline void size24(uint64_t size) { l2p24((uint32_t)size, _data + 14); }
	inline bool sizeChanged(uint64_t size) { return (size & 0xffffff) != size24(); }

	// for dir item (parent/hist, sub)
	// uint24(_data + 11): isdel() ? parent : hist
	inline uint32_t parent() const { return p2l24(_data + 11); }
	inline void parent(uint32_t pid) { AuVerify(pid < (1 << 24)); l2p24(pid, _data + 11); }

	// uint24(_data + 14)
	inline uint32_t sub() const { return p2l24(_data + 14); }
	inline void sub(uint32_t subid) { AuVerify(subid < (1 << 24)); l2p24(subid, _data + 14); }

	// flags uint8(_data + 17): isdir, isdeleted, isslink, ishlink, isexe
private:
	inline bool getflag(int bit) const { return (_data[17] & (1 << bit)) != 0; }
	inline void setflag(bool flag, int bit) { if (flag) _data[17] |= (1u << bit); else _data[17] &= ~(1u << bit); }
public:
	inline bool isdir() const { return getflag(0); }
	inline void isdir(bool flag) { setflag(flag, 0); }
	inline bool isdel() const { return getflag(1); }
	inline void isdel(bool flag) { setflag(flag, 1); }
	inline bool isslink() const { return getflag(2); }
	inline void isslink(bool flag) { setflag(flag, 2); }
	inline bool ishlink() const { return getflag(3); }
	inline void ishlink(bool flag) { setflag(flag, 3); }
	inline bool isexe() const { return getflag(4); }
	inline void isexe(bool flag) { setflag(flag, 4); }

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

protected:
	// name: 4; time: 4; next: 3; union{dir(sub: 3, parent: 3), file(hist: 3, size: 3)}, flag: 1
	uint8_t _data[4 + 4 + 3 + 3 + 3 + 1]/* = { 0 }*/;
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