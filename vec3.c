#include <math.h>

#include "vec3.h"

float vec3_dot(const union vec3 *v1, const union vec3 *v2)
{
	return v1->x * v2->x + v1->y * v2->y + v1->z * v2->z;
}

/* returns square of the length of a vector */
float vec3_len2(const union vec3 *v)
{
	return v->x * v->x + v->y * v->y + v->z * v->z;
}

union vec3 *vec3_normalize(union vec3 *vo, const union vec3 *vi)
{
	float len = sqrt(vec3_len2(vi));
	vo->x = vi->x / len;
	vo->y = vi->y / len;
	vo->z = vi->z / len;
	return vo;
}

union vec3 *vec3_normalize_self(union vec3 *vo)
{
	return vec3_normalize(vo, vo);
}

