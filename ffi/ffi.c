#define MODULE ffi
#include <sqmodule.h>
#include <stdio.h>
#include "sqmodapi.h"
#include <dlfcn.h>
#include <ffi.h>

DECLARE_SQAPI

static HSQOBJECT method_table;

static SQInteger m_dlopen(HSQUIRRELVM v)
{
    const SQChar *fname;
    sq_getstring(v, 2, &fname);
    void *mod = dlopen(fname, RTLD_NOW | RTLD_LOCAL);
    sq_pushuserpointer(v, mod);
    return 1;
}

static SQInteger m_dlsym(HSQUIRRELVM v)
{
    void *mod;
    const SQChar *symname;
    sq_getuserpointer(v, 2, &mod);
    sq_getstring(v, 3, &symname);
    void *sym = dlsym(mod, symname);
    printf("dlsym(%s) = %p\n", symname, sym);
    ((int(*)(char*))sym)("Call test\n");
    sq_pushuserpointer(v, sym);
    return 1;
}

static ffi_type *get_ffi_type(const char *t)
{
    switch (*t) {
    case 'I':
        return &ffi_type_uint;
    case 'P':
        return &ffi_type_pointer;
    }
    return &ffi_type_uint;
}

struct FFIData
{
    void *func;
    ffi_cif cif;
    ffi_type *params[0];
};

static SQInteger m_ffi_prepare(HSQUIRRELVM v)
{
    void *sym;
    const char *rettype;
    sq_getuserpointer(v, 2, &sym);
    sq_getstring(v, 3, &rettype);
    int nparam = sq_getsize(v, 4);

    int size = sizeof(struct FFIData) + sizeof(ffi_type*) * nparam;
    struct FFIData *ffibuf = (struct FFIData *)sq_newuserdata(v, size);
    sq_pushobject(v, method_table);
    sq_setdelegate(v, -2);

    printf("Allocated %d bytes at %p\n", size, ffibuf);
    ffibuf->func = sym;

    int i;
    for (i = 0; i < nparam; i++) {
        const char *paramtype;
        sq_pushinteger(v, i);
        sq_get(v, 4);
        if (sq_gettype(v, -1) != OT_STRING)
            return sq_throwerror(v, "Parameter types must be strings");
        sq_getstring(v, -1, &paramtype);
        ffibuf->params[i] = get_ffi_type(paramtype);
        sq_poptop(v);
    }
    int res = ffi_prep_cif(&ffibuf->cif, FFI_DEFAULT_ABI, nparam, get_ffi_type(rettype), ffibuf->params);
    if (res != FFI_OK)
        return sq_throwerror(v, "Error in ffi_prep_cif");
    return 1;
}

#define METAMETHOD
#ifdef METAMETHOD
// For metamethod, we have userdata, then delegate table
#define EXTRA_PARAMS 2
#else
// For normal method, there's only userdata
#define EXTRA_PARAMS 1
#endif

static SQInteger m_ffi_call(HSQUIRRELVM v)
{
    struct FFIData *ffibuf;
    sq_getuserdata(v, 1, (void**)&ffibuf, NULL);
    int top = sq_gettop(v);
    printf("ffibuf %p top %d\n", ffibuf, top);

    if (ffibuf->cif.nargs != top - EXTRA_PARAMS)
        return sq_throwerror(v, "Wrong number of args");

    int values[top - EXTRA_PARAMS];
    void *valueptrs[top - EXTRA_PARAMS];
    int i;
    for (i = EXTRA_PARAMS + 1; i <= top; i++) {
        #define pi (i - (EXTRA_PARAMS + 1))
        switch (sq_gettype(v, i)) {
        case OT_STRING:
            sq_getstring(v, i, (const char**)&values[pi]);
            break;
        default:
            values[pi] = 0;
        }
        valueptrs[pi] = &values[pi];
    }
    int rc;
    printf("Before call, %p\n", ffibuf->func);
    ffi_call(&ffibuf->cif, ffibuf->func, &rc, valueptrs);
    sq_pushinteger(v, rc);
    return 1;
}

static void sq_register_funcs(HSQUIRRELVM sqvm, SQRegFunction *obj_funcs) {
   SQInteger i = 0;
   while (obj_funcs[i].name != 0) {
       SQRegFunction *f = &obj_funcs[i];
       sq_pushstring(sqvm, f->name, -1);
       sq_newclosure(sqvm, f->f, 0);
       sq_setparamscheck(sqvm, f->nparamscheck, f->typemask);
       sq_setnativeclosurename(sqvm, -1, f->name);
       sq_newslot(sqvm, -3, SQFalse);
       i++;
   }
}

static SQRegFunction funcs[] = {
    {"dlopen", m_dlopen, 2, _SC(".s")},
    {"dlsym", m_dlsym, 3, _SC(".ps")},
    {"ffi", m_ffi_prepare, 4, _SC(".psa")},
    {NULL}
};

static SQRegFunction methods[] = {
    {"_call",  m_ffi_call, -1, _SC("u")},
    {NULL}
};

SQRESULT MODULE_INIT(HSQUIRRELVM v, HSQAPI api)
{
    printf("in sqmodule_load\n");

    INIT_SQAPI(v, api);

    sq_register_funcs(v, funcs);

    sq_newtable(v);
    sq_register_funcs(v, methods);
    sq_resetobject(&method_table);
    sq_getstackobj(v, -1, &method_table);
    sq_addref(v, &method_table);

    printf("out sqmodule_load\n");

    return SQ_OK;
}