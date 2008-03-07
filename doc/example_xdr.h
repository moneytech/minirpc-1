/* AUTOGENERATED FILE -- DO NOT EDIT */

/**
 * @file
 * @brief Data structures for the example protocol
 * @addtogroup example_common
 * @{
 */

/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#ifndef EXAMPLE_XDR_H
#define EXAMPLE_XDR_H

#include <rpc/rpc.h>


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generated enum corresponding to Color XDR type
 */
enum Color {
	RED = 0,
	ORANGE = 1,
	YELLOW = 2,
	GREEN = 3,
	BLUE = 4,
	INDIGO = 5,
	VIOLET = 6,
};
/**
 * @brief Generated typedef for Color XDR type
 */
typedef enum Color Color;

/**
 * @brief Generated struct corresponding to ColorChoice XDR type
 */
struct ColorChoice {
	struct {
		u_int acceptable_len;
		enum Color *acceptable_val;
	} acceptable;
	enum Color preferred;
};
/**
 * @brief Generated typedef for ColorChoice XDR type
 */
typedef struct ColorChoice ColorChoice;

/**
 * @brief Generated typedef corresponding to Count XDR typedef
 */
typedef int Count;


/* The xdr functions.  The application should never need to call these
   directly. */
/**
 * @cond SHOW_XDR_PROCS
 */
#if defined(__STDC__) || defined(__cplusplus)
extern  bool_t xdr_Color (XDR *, Color*);
extern  bool_t xdr_ColorChoice (XDR *, ColorChoice*);
extern  bool_t xdr_Count (XDR *, Count*);

#else /* K&R C */
extern bool_t xdr_Color ();
extern bool_t xdr_ColorChoice ();
extern bool_t xdr_Count ();

#endif /* K&R C */
/**
 * @endcond
 */

#ifdef __cplusplus
}
#endif

#endif /* !EXAMPLE_XDR_H */

/**
 * @}
 */
