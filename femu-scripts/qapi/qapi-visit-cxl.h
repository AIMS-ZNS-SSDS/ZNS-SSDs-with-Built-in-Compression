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

#ifndef QAPI_VISIT_CXL_H
#define QAPI_VISIT_CXL_H

#include "qapi/qapi-builtin-visit.h"
#include "qapi-types-cxl.h"


bool visit_type_CxlEventLog(Visitor *v, const char *name,
                 CxlEventLog *obj, Error **errp);

bool visit_type_q_obj_cxl_inject_general_media_event_arg_members(Visitor *v, q_obj_cxl_inject_general_media_event_arg *obj, Error **errp);

bool visit_type_q_obj_cxl_inject_dram_event_arg_members(Visitor *v, q_obj_cxl_inject_dram_event_arg *obj, Error **errp);

bool visit_type_q_obj_cxl_inject_memory_module_event_arg_members(Visitor *v, q_obj_cxl_inject_memory_module_event_arg *obj, Error **errp);

bool visit_type_q_obj_cxl_inject_poison_arg_members(Visitor *v, q_obj_cxl_inject_poison_arg *obj, Error **errp);

bool visit_type_CxlUncorErrorType(Visitor *v, const char *name,
                 CxlUncorErrorType *obj, Error **errp);

bool visit_type_CXLUncorErrorRecord_members(Visitor *v, CXLUncorErrorRecord *obj, Error **errp);

bool visit_type_CXLUncorErrorRecord(Visitor *v, const char *name,
                 CXLUncorErrorRecord **obj, Error **errp);

bool visit_type_CXLUncorErrorRecordList(Visitor *v, const char *name,
                 CXLUncorErrorRecordList **obj, Error **errp);

bool visit_type_q_obj_cxl_inject_uncorrectable_errors_arg_members(Visitor *v, q_obj_cxl_inject_uncorrectable_errors_arg *obj, Error **errp);

bool visit_type_CxlCorErrorType(Visitor *v, const char *name,
                 CxlCorErrorType *obj, Error **errp);

bool visit_type_q_obj_cxl_inject_correctable_error_arg_members(Visitor *v, q_obj_cxl_inject_correctable_error_arg *obj, Error **errp);

#endif /* QAPI_VISIT_CXL_H */
