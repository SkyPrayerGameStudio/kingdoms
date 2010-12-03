#ifndef BUF2D_H
#define BUF2D_H

#include "utils.h"

template<typename N>
class buf2d {
	public:
		buf2d(int x, int y, const N& def);
		~buf2d();
		buf2d(const buf2d& buf);
		buf2d& operator=(const buf2d& buf);
		const N* get(int x, int y) const;
		N* get_mod(int x, int y);
		void set(int x, int y, const N& val);
		int size_x;
		int size_y;
	private:
		int get_index(int x, int y) const;
		N* data;
};

template<typename N>
buf2d<N>::buf2d(int x, int y, const N& def)
	: size_x(x),
	size_y(y),
	data(new N[x * y])
{
	for(int i = 0; i < y; i++) {
		for(int j = 0; j < x; j++) {
			data[get_index(j, i)] = def;
		}
	}
}

template<typename N>
buf2d<N>::~buf2d()
{
	delete[] this->data;
}

template<typename N>
buf2d<N>::buf2d(const buf2d& buf)
	: size_x(buf.size_x),
	size_y(buf.size_y),
	data(new N[buf.size_x * buf.size_y])
{
	std::copy(buf.data, buf.data + buf.size_x * buf.size_y, data);
}

template<typename N>
buf2d<N>& buf2d<N>::operator=(const buf2d& buf)
{
	if(this != &buf) {
		N* nd = new N[buf.size_x * buf.size_y];
		std::copy(buf.data, buf.data + buf.size_x * buf.size_y, nd);
		delete[] this->data;
		this->data = nd;
		size_x = buf.size_x;
		size_y = buf.size_y;
	}
	return *this;
}

template<typename N>
int buf2d<N>::get_index(int x, int y) const
{
	return y * size_x + x;
}

template<typename N>
void buf2d<N>::set(int x, int y, const N& val)
{
	if(!in_bounds(0, x, size_x - 1) || !in_bounds(0, y, size_y - 1))
		return;
	data[get_index(x, y)] = val;
}

template<typename N>
const N* buf2d<N>::get(int x, int y) const
{
	if(!in_bounds(0, x, size_x - 1) || !in_bounds(0, y, size_y - 1))
		return NULL;
	return &data[get_index(x, y)];
}

template<typename N>
N* buf2d<N>::get_mod(int x, int y)
{
	if(!in_bounds(0, x, size_x - 1) || !in_bounds(0, y, size_y - 1))
		return NULL;
	return &data[get_index(x, y)];
}

#endif

