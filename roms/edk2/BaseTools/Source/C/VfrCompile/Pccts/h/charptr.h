/*
 *
 * SOFTWARE RIGHTS
 *
 * We reserve no LEGAL rights to the Purdue Compiler Construction Tool
 * Set (PCCTS) -- PCCTS is in the public domain.  An individual or
 * company may do whatever they wish with source code distributed with
 * PCCTS or the code generated by PCCTS, including the incorporation of
 * PCCTS, or its output, into commerical software.
 *
 * We encourage users to develop software with PCCTS.  However, we do ask
 * that credit is given to us for developing PCCTS.  By "credit",
 * we mean that if you incorporate our source code into one of your
 * programs (commercial product, research project, or otherwise) that you
 * acknowledge this fact somewhere in the documentation, research report,
 * etc...  If you like PCCTS and have developed a nice tool with the
 * output, please mention that you developed it using PCCTS.  In
 * addition, we ask that this header remain intact in our source code.
 * As long as these guidelines are kept, we expect to continue enhancing
 * this system and expect to make other tools available as they are
 * completed.
 *
 * ANTLR 1.33
 * Terence Parr
 * Parr Research Corporation
 * with Purdue University and AHPCRC, University of Minnesota
 * 1989-2000
 */

/*
 * WARNING!!!!: charptr.h does NOT make copies and the
 * memory is freed after the attribute scope exits.
 */

#ifndef ZZCHARPTR_H
#define ZZCHARPTR_H

typedef char *Attrib;
#define zzdef0(a)		{*(a)=NULL;}
/* MR8  Jens Tingleff (jensting@imaginet.fr)                                */
/*          Set memory pointer to null after free()                         */
#define zzd_attr(a)		{if ( *(a)!=NULL ) {free(*(a)); *(a)=NULL; }; }

#ifdef __STDC__
extern zzcr_attr(Attrib *,int,char *);
#endif

#endif
