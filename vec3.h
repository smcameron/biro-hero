#ifndef VEC3_H__
#define VEC3_H__

/* generic 3d vector */
union vec3 {
	struct {
		float x;
		float y;
		float z;
	};
	struct {
		float r;
		float g;
		float b;
	};
	float vec[3];
};
#define VEC3_INITIALIZER { { 0.0, 0.0, 0.0 } }

/* returns square of the magnitude of vector *v */
float vec3_len2(const union vec3 *v);

/* vo = vector output, vi = vector input.  vo <- normalized(vi) */
union vec3 *vec3_normalize(union vec3 *vo, const union vec3 *vi);
union vec3 *vec3_normalize_self(union vec3 *vo);

/* dot product */
float vec3_dot(const union vec3 *v1, const union vec3 *v2);

#endif

