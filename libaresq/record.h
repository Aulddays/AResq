#include <stdint.h>

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
	inline uint32_t name() const { return p2l32(_data); }
	inline void name(uint32_t name) { l2p32(name, _data); }

	inline uint32_t time() const { return p2l32(_data + 4); }
	inline void time(uint32_t time) { l2p32(time, _data + 4); }

	inline uint32_t next() const { return p2l24(_data + 8); }
	inline void next(uint32_t nid) { l2p24(nid, _data + 8); }

	// for file item
	inline bool hidden() const { return _data[11] & 1; }
	inline void hidden(bool flag) { _data[11] = flag ? (_data[11] | 1) : (_data[11] & ~(1u)); }
	inline bool readonly() const { return (_data[11] & 2) != 0; }
	inline void readonly(bool flag) { _data[11] = flag ? (_data[11] | 2) : (_data[11] & ~(2u)); }

	inline uint32_t size16() const { return p2l16(_data + 12); }
	inline uint32_t size16(size_t size) { l2p16(size, _data + 12); }
	inline bool sizeChanged(size_t size) { return (uint32_t)(size & 0xffffffff) == size16(); }

	// for dir item
	inline uint32_t sub() const { return p2l24(_data + 11); }
	inline void sub(uint32_t sid) { l2p24(sid, _data + 11); }

public:
	RecordItem()
	{
		for (int i = 0; i < sizeof(_data); ++i)
			_data[i] = 0;
	}

protected:
	// name: 4 (isdir: 1bit); time: 4; next: 3; union{{flag: 1, size: 2}, sub: 3}
	uint8_t _data[4 + 4 + 3 + 3]/* = { 0 }*/;
};

#pragma pack(pop)