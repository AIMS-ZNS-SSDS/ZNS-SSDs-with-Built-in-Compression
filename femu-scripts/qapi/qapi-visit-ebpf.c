/* AUTOMATICALLY GENERATED by qapi-gen.py DO NOT MODIFY */

/*
 * Schema-defined QAPI visitors
 *
 * Copyright IBM, Corp. 2011
 * Copyright (C) 2014-2018 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qapi-visit-ebpf.h"

#if defined(CONFIG_EBPF)
bool visit_type_EbpfObject_members(Visitor *v, EbpfObject *obj, Error **errp)
{
    if (!visit_type_str(v, "object", &obj->object, errp)) {
        return false;
    }
    return true;
}

bool visit_type_EbpfObject(Visitor *v, const char *name,
                 EbpfObject **obj, Error **errp)
{
    bool ok = false;

    if (!visit_start_struct(v, name, (void **)obj, sizeof(EbpfObject), errp)) {
        return false;
    }
    if (!*obj) {
        /* incomplete */
        assert(visit_is_dealloc(v));
        ok = true;
        goto out_obj;
    }
    if (!visit_type_EbpfObject_members(v, *obj, errp)) {
        goto out_obj;
    }
    ok = visit_check_struct(v, errp);
out_obj:
    visit_end_struct(v, (void **)obj);
    if (!ok && visit_is_input(v)) {
        qapi_free_EbpfObject(*obj);
        *obj = NULL;
    }
    return ok;
}
#endif /* defined(CONFIG_EBPF) */

#if defined(CONFIG_EBPF)
bool visit_type_EbpfProgramID(Visitor *v, const char *name,
                 EbpfProgramID *obj, Error **errp)
{
    int value = *obj;
    bool ok = visit_type_enum(v, name, &value, &EbpfProgramID_lookup, errp);
    *obj = value;
    return ok;
}
#endif /* defined(CONFIG_EBPF) */

#if defined(CONFIG_EBPF)
bool visit_type_q_obj_request_ebpf_arg_members(Visitor *v, q_obj_request_ebpf_arg *obj, Error **errp)
{
    if (!visit_type_EbpfProgramID(v, "id", &obj->id, errp)) {
        return false;
    }
    return true;
}
#endif /* defined(CONFIG_EBPF) */

/* Dummy declaration to prevent empty .o file */
char qapi_dummy_qapi_visit_ebpf_c;
