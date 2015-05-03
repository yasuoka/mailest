/*************************************************************************************************
 * The command line interface for the core API
 *                                                      Copyright (C) 2004-2007 Mikio Hirabayashi
 * This file is part of Hyper Estraier.
 * Hyper Estraier is free software; you can redistribute it and/or modify it under the terms of
 * the GNU Lesser General Public License as published by the Free Software Foundation; either
 * version 2.1 of the License or any later version.  Hyper Estraier is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 * You should have received a copy of the GNU Lesser General Public License along with Hyper
 * Estraier; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307 USA.
 *************************************************************************************************/

#include <stdio.h>
#include <string.h>
#include <estraier.h>
#include <assert.h>

#include "estdraft.h"

#define NUMBUFSIZ      32                /* size of a buffer for a number */
#define MINIBNUM       31                /* bucket number of a small map */
#define TRUE           1
#define FALSE          0

static ESTDOC *est_doc_new_from_text(const char *buf, int size,
                                     const char *penc, int plang, int bcheck);
static ESTDOC *est_doc_new_from_html(const char *buf, int size,
                                     const char *penc, int plang, int bcheck);
static void est_doc_add_attr_mime(ESTDOC *doc, const char *name, const char *value);
static int est_check_binary(const char *buf, int size);
static char *est_html_enc(const char *str);
static char *est_html_raw_text(const char *html);

/* check whether a buffer is binary */
static int est_check_binary(const char *buf, int size){
  int i, bin;
  assert(buf && size >= 0);
  if(size < 32) return FALSE;
  /* PDF */
  if(!memcmp(buf, "%PDF-", 5)) return TRUE;
  /* PostScript */
  if(!memcmp(buf, "%!PS-Adobe", 10)) return TRUE;
  /* generic binary */
  size -= 5;
  if(size >= 256) size = 256;
  bin = FALSE;
  for(i = 0; i < size; i++){
    if(buf[i] == 0x0){
      if(buf[i+1] == 0x0 && buf[i+2] == 0x0 && buf[i+3] == 0x0 && buf[i+4] == 0x0) return TRUE;
      bin = TRUE;
    }
  }
  if(!bin) return FALSE;
  /* PNG */
  if(!memcmp(buf, "\x89PNG", 4)) return TRUE;
  /* GIF(87a) */
  if(!memcmp(buf, "GIF87a", 6)) return TRUE;
  /* GIF(89a) */
  if(!memcmp(buf, "GIF89a", 6)) return TRUE;
  /* JFIF */
  if(!memcmp(buf, "\xff\xd8JFIF", 6)) return TRUE;
  /* TIFF(Intel) */
  if(!memcmp(buf, "MM\x00\x2a", 4)) return TRUE;
  /* TIFF(Motorola) */
  if(!memcmp(buf, "II\x2a\x00", 4)) return TRUE;
  /* BMP */
  if(!memcmp(buf, "BM", 2)) return TRUE;
  /* GZIP */
  if(!memcmp(buf, "\x1f\x8b\x08", 3)) return TRUE;
  /* BZIP2 */
  if(!memcmp(buf, "BZh", 3)) return TRUE;
  /* ZIP */
  if(!memcmp(buf, "PK\x03\x04", 4)) return TRUE;
  /* MP3(with ID3) */
  if(!memcmp(buf, "ID3", 3)) return TRUE;
  /* MP3 */
  if(((buf[0] * 0x100 + buf[1]) & 0xfffe) == 0xfffa) return TRUE;
  /* MIDI */
  if(!memcmp(buf, "MThd", 4)) return TRUE;
  /* RPM package*/
  if(!memcmp(buf, "0xed0xab", 2)) return TRUE;
  /* Debian package */
  if(!memcmp(buf, "!<arch>\ndebian", 14)) return TRUE;
  /* ELF */
  if(!memcmp(buf, "\x7f\x45\x4c\x46", 4)) return TRUE;
  /* MS-DOS executable */
  if(!memcmp(buf, "MZ", 2)) return TRUE;
  /* MS-Office */
  if(!memcmp(buf, "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1", 8)) return TRUE;
  if(!memcmp(buf, "\xfe\x37\x00\x23", 4)) return TRUE;
  if(!memcmp(buf, "\xdb\xa5-\x00\x00\x00", 6)) return TRUE;
  return FALSE;
}

/* create a document object from plain text */
static ESTDOC *est_doc_new_from_text(const char *buf, int size,
                                     const char *penc, int plang, int bcheck){
  ESTDOC *doc;
  CBLIST *lines;
  CBDATUM *datum;
  const char *enc, *text, *line;
  char *nbuf, numbuf[NUMBUFSIZ];
  int i;
  assert(buf && size >= 0);
  if(bcheck && est_check_binary(buf, size)) return NULL;
  doc = est_doc_new();
  enc = penc ? penc : est_enc_name(buf, size, plang);
  if(!strcmp(enc, "UTF-8")){
    nbuf = NULL;
    text = buf;
  } else {
    text = buf;
    nbuf = est_iconv(buf, size, enc, "UTF-8", NULL, NULL);
    if(nbuf) text = nbuf;
  }
  lines = cbsplit(text, -1, "\n");
  CB_DATUMOPEN(datum);
  for(i = 0; i < CB_LISTNUM(lines); i++){
    line = CB_LISTVAL(lines, i);
    while(*line == ' ' || *line == '\t' || *line == '\r'){
      line++;
    }
    if(line[0] == '\0'){
      est_doc_add_text(doc, CB_DATUMPTR(datum));
      CB_DATUMSETSIZE(datum, 0);
    } else {
      CB_DATUMCAT(datum, " ", 1);
      CB_DATUMCAT(datum, line, (ssize_t)strlen(line));
    }
  }
  est_doc_add_text(doc, CB_DATUMPTR(datum));
  CB_DATUMCLOSE(datum);
  CB_LISTCLOSE(lines);
  est_doc_add_attr(doc, ESTDATTRTYPE, "text/plain");
  sprintf(numbuf, "%d", size);
  est_doc_add_attr(doc, ESTDATTRSIZE, numbuf);
  if(nbuf) free(nbuf);
  return doc;
}


/* create a document object from HTML */
static ESTDOC *est_doc_new_from_html(const char *buf, int size,
                                     const char *penc, int plang, int bcheck){
  ESTDOC *doc;
  CBLIST *elems;
  CBMAP *attrs;
  CBDATUM *datum;
  const char *enc, *html, *elem, *next, *value, *name, *content;
  char *nbuf, *nenc, *rbuf, *lbuf, numbuf[NUMBUFSIZ];
  int i, esiz;
  assert(buf && size >= 0);
  if(bcheck && est_check_binary(buf, size)) return NULL;
  doc = est_doc_new();
  enc = est_enc_name(buf, size, plang);
  html = NULL;
  nbuf = NULL;
  if(!strcmp(enc, "UTF-16") || !strcmp(enc, "UTF-16BE") || !strcmp(enc, "UTF-16LE")){
    nbuf = est_iconv(buf, size, enc, "UTF-8", NULL, NULL);
  } else if(!strcmp(enc, "US-ASCII")){
    nbuf = NULL;
  } else {
    if((nenc = penc ? cbmemdup(penc, -1) : est_html_enc(buf)) != NULL){
      if(cbstricmp(nenc, "UTF-8")){
        nbuf = est_iconv(buf, size, nenc, "UTF-8", NULL, NULL);
        if(!nbuf) nbuf = est_iconv(buf, size, enc, "UTF-8", NULL, NULL);
      }
      free(nenc);
    } else {
      nbuf = est_iconv(buf, size, enc, "UTF-8", NULL, NULL);
    }
  }
  if(nbuf) html = nbuf;
  if(!html) html = buf;
  CB_DATUMOPEN(datum);
  elems = cbxmlbreak(html, TRUE);
  for(i = 0; i < CB_LISTNUM(elems); i++){
    elem = CB_LISTVAL2(elems, i, esiz);
    if(!(next = cblistval(elems, i + 1, NULL))) next = "";
    if(elem[0] == '<'){
      if(cbstrfwimatch(elem, "<html")){
        attrs = cbxmlattrs(elem);
        value = cbmapget(attrs, "lang", -1, NULL);
        if(!value) value = cbmapget(attrs, "Lang", -1, NULL);
        if(!value) value = cbmapget(attrs, "LANG", -1, NULL);
        if(!value) value = cbmapget(attrs, "xml:lang", -1, NULL);
        if(value && value[0] != '\0') est_doc_add_attr(doc, ESTDATTRLANG, value);
        cbmapclose(attrs);
      } else if(cbstrfwimatch(elem, "<meta")){
        attrs = cbxmlattrs(elem);
        name = cbmapget(attrs, "name", -1, NULL);
        if(!name) name = cbmapget(attrs, "Name", -1, NULL);
        if(!name) name = cbmapget(attrs, "NAME", -1, NULL);
        if(!name) name = cbmapget(attrs, "http-equiv", -1, NULL);
        if(!name) name = cbmapget(attrs, "Http-equiv", -1, NULL);
        if(!name) name = cbmapget(attrs, "Http-Equiv", -1, NULL);
        if(!name) name = cbmapget(attrs, "HTTP-EQUIV", -1, NULL);
        content = cbmapget(attrs, "content", -1, NULL);
        if(!content) content = cbmapget(attrs, "Content", -1, NULL);
        if(!content) content = cbmapget(attrs, "CONTENT", -1, NULL);
        if(name && content){
          lbuf = cbmemdup(name, -1);
          cbstrtolower(lbuf);
          cbstrsqzspc(lbuf);
          if(!strcmp(lbuf, "author")){
            if(strchr(content, '&')){
              rbuf = est_html_raw_text(content);
              est_doc_add_attr(doc, ESTDATTRAUTHOR, rbuf);
              free(rbuf);
            } else {
              est_doc_add_attr(doc, ESTDATTRAUTHOR, content);
            }
          }
          if(name[0] != '@' && name[0] != '_'){
            if(strchr(content, '&')){
              rbuf = est_html_raw_text(content);
              est_doc_add_attr(doc, lbuf, rbuf);
              free(rbuf);
            } else {
              est_doc_add_attr(doc, lbuf, content);
            }
          }
          free(lbuf);
        }
        cbmapclose(attrs);
      } else if(cbstrfwimatch(elem, "<title") && next[0] != '\0' && next[0] != '<'){
        if(strchr(next, '&')){
          rbuf = est_html_raw_text(next);
          est_doc_add_attr(doc, ESTDATTRTITLE, rbuf);
          est_doc_add_hidden_text(doc, rbuf);
          free(rbuf);
        } else {
          est_doc_add_attr(doc, ESTDATTRTITLE, next);
          est_doc_add_hidden_text(doc, next);
        }
        i++;
      } else if(cbstrfwimatch(elem, "<style") || cbstrfwimatch(elem, "<script")){
        while((next = cblistval(elems, i + 1, NULL)) != NULL &&
              !(next[0] == '<' && next[1] != '!' && next[1] != ' ' && next[1] != '=')){
          i++;
        }
      } else if(cbstrfwimatch(elem, "<h1") || cbstrfwimatch(elem, "<h2") ||
                cbstrfwimatch(elem, "<h3") || cbstrfwimatch(elem, "<h4") ||
                cbstrfwimatch(elem, "<h5") || cbstrfwimatch(elem, "<h6") ||
                cbstrfwimatch(elem, "<p>") || cbstrfwimatch(elem, "<p ") ||
                cbstrfwimatch(elem, "<div") || cbstrfwimatch(elem, "<hr") ||
                cbstrfwimatch(elem, "<ul") || cbstrfwimatch(elem, "<ol") ||
                cbstrfwimatch(elem, "<dl") || cbstrfwimatch(elem, "<li") ||
                cbstrfwimatch(elem, "<dt") || cbstrfwimatch(elem, "<dd") ||
                cbstrfwimatch(elem, "<th") || cbstrfwimatch(elem, "<td") ||
                cbstrfwimatch(elem, "<pre")){
        if(strchr(CB_DATUMPTR(datum), '&')){
          rbuf = est_html_raw_text(CB_DATUMPTR(datum));
          est_doc_add_text(doc, rbuf);
          free(rbuf);
        } else {
          est_doc_add_text(doc, CB_DATUMPTR(datum));
        }
        CB_DATUMSETSIZE(datum, 0);
      }
    } else {
      CB_DATUMCAT(datum, " ", 1);
      CB_DATUMCAT(datum, elem, esiz);
    }
  }
  CB_LISTCLOSE(elems);
  if(strchr(CB_DATUMPTR(datum), '&')){
    rbuf = est_html_raw_text(CB_DATUMPTR(datum));
    est_doc_add_text(doc, rbuf);
    free(rbuf);
  } else {
    est_doc_add_text(doc, CB_DATUMPTR(datum));
  }
  CB_DATUMCLOSE(datum);
  if(nbuf) free(nbuf);
  est_doc_add_attr(doc, ESTDATTRTYPE, "text/html");
  sprintf(numbuf, "%d", size);
  est_doc_add_attr(doc, ESTDATTRSIZE, numbuf);
  return doc;
}

/* get the encoding of an HTML string */
static char *est_html_enc(const char *str){
  CBLIST *elems;
  CBMAP *attrs;
  const char *elem, *equiv, *content;
  char *enc, *pv;
  int i;
  assert(str);
  elems = cbxmlbreak(str, TRUE);
  for(i = 0; i < CB_LISTNUM(elems); i++){
    elem = CB_LISTVAL(elems, i);
    if(elem[0] != '<' || !cbstrfwimatch(elem, "<meta")) continue;
    enc = NULL;
    attrs = cbxmlattrs(elem);
    equiv = cbmapget(attrs, "http-equiv", -1, NULL);
    if(!equiv) equiv = cbmapget(attrs, "HTTP-EQUIV", -1, NULL);
    if(!equiv) equiv = cbmapget(attrs, "Http-Equiv", -1, NULL);
    if(!equiv) equiv = cbmapget(attrs, "Http-equiv", -1, NULL);
    if(equiv && !cbstricmp(equiv, "Content-Type")){
      content = cbmapget(attrs, "content", -1, NULL);
      if(!content) content = cbmapget(attrs, "Content", -1, NULL);
      if(!content) content = cbmapget(attrs, "CONTENT", -1, NULL);
      if(content && ((pv = strstr(content, "charset")) != NULL ||
                     (pv = strstr(content, "Charset")) != NULL ||
                     (pv = strstr(content, "CHARSET")) != NULL)){
        enc = cbmemdup(pv + 8, -1);
        if((pv = strchr(enc, ';')) != NULL || (pv = strchr(enc, '\r')) != NULL ||
           (pv = strchr(enc, '\n')) != NULL || (pv = strchr(enc, ' ')) != NULL) *pv = '\0';
      }
    }
    cbmapclose(attrs);
    if(enc){
      CB_LISTCLOSE(elems);
      return enc;
    }
  }
  CB_LISTCLOSE(elems);
  return NULL;
}

/* unescape entity references of HTML */
static char *est_html_raw_text(const char *html){
  static const char *pairs[] = {
    /* basic symbols */
    "&amp;", "&", "&lt;", "<", "&gt;", ">", "&quot;", "\"", "&apos;", "'",
    /* ISO-8859-1 */
    "&nbsp;", "\xc2\xa0", "&iexcl;", "\xc2\xa1", "&cent;", "\xc2\xa2",
    "&pound;", "\xc2\xa3", "&curren;", "\xc2\xa4", "&yen;", "\xc2\xa5",
    "&brvbar;", "\xc2\xa6", "&sect;", "\xc2\xa7", "&uml;", "\xc2\xa8",
    "&copy;", "\xc2\xa9", "&ordf;", "\xc2\xaa", "&laquo;", "\xc2\xab",
    "&not;", "\xc2\xac", "&shy;", "\xc2\xad", "&reg;", "\xc2\xae",
    "&macr;", "\xc2\xaf", "&deg;", "\xc2\xb0", "&plusmn;", "\xc2\xb1",
    "&sup2;", "\xc2\xb2", "&sup3;", "\xc2\xb3", "&acute;", "\xc2\xb4",
    "&micro;", "\xc2\xb5", "&para;", "\xc2\xb6", "&middot;", "\xc2\xb7",
    "&cedil;", "\xc2\xb8", "&sup1;", "\xc2\xb9", "&ordm;", "\xc2\xba",
    "&raquo;", "\xc2\xbb", "&frac14;", "\xc2\xbc", "&frac12;", "\xc2\xbd",
    "&frac34;", "\xc2\xbe", "&iquest;", "\xc2\xbf", "&Agrave;", "\xc3\x80",
    "&Aacute;", "\xc3\x81", "&Acirc;", "\xc3\x82", "&Atilde;", "\xc3\x83",
    "&Auml;", "\xc3\x84", "&Aring;", "\xc3\x85", "&AElig;", "\xc3\x86",
    "&Ccedil;", "\xc3\x87", "&Egrave;", "\xc3\x88", "&Eacute;", "\xc3\x89",
    "&Ecirc;", "\xc3\x8a", "&Euml;", "\xc3\x8b", "&Igrave;", "\xc3\x8c",
    "&Iacute;", "\xc3\x8d", "&Icirc;", "\xc3\x8e", "&Iuml;", "\xc3\x8f",
    "&ETH;", "\xc3\x90", "&Ntilde;", "\xc3\x91", "&Ograve;", "\xc3\x92",
    "&Oacute;", "\xc3\x93", "&Ocirc;", "\xc3\x94", "&Otilde;", "\xc3\x95",
    "&Ouml;", "\xc3\x96", "&times;", "\xc3\x97", "&Oslash;", "\xc3\x98",
    "&Ugrave;", "\xc3\x99", "&Uacute;", "\xc3\x9a", "&Ucirc;", "\xc3\x9b",
    "&Uuml;", "\xc3\x9c", "&Yacute;", "\xc3\x9d", "&THORN;", "\xc3\x9e",
    "&szlig;", "\xc3\x9f", "&agrave;", "\xc3\xa0", "&aacute;", "\xc3\xa1",
    "&acirc;", "\xc3\xa2", "&atilde;", "\xc3\xa3", "&auml;", "\xc3\xa4",
    "&aring;", "\xc3\xa5", "&aelig;", "\xc3\xa6", "&ccedil;", "\xc3\xa7",
    "&egrave;", "\xc3\xa8", "&eacute;", "\xc3\xa9", "&ecirc;", "\xc3\xaa",
    "&euml;", "\xc3\xab", "&igrave;", "\xc3\xac", "&iacute;", "\xc3\xad",
    "&icirc;", "\xc3\xae", "&iuml;", "\xc3\xaf", "&eth;", "\xc3\xb0",
    "&ntilde;", "\xc3\xb1", "&ograve;", "\xc3\xb2", "&oacute;", "\xc3\xb3",
    "&ocirc;", "\xc3\xb4", "&otilde;", "\xc3\xb5", "&ouml;", "\xc3\xb6",
    "&divide;", "\xc3\xb7", "&oslash;", "\xc3\xb8", "&ugrave;", "\xc3\xb9",
    "&uacute;", "\xc3\xba", "&ucirc;", "\xc3\xbb", "&uuml;", "\xc3\xbc",
    "&yacute;", "\xc3\xbd", "&thorn;", "\xc3\xbe", "&yuml;", "\xc3\xbf",
    /* ISO-10646 */
    "&fnof;", "\xc6\x92", "&Alpha;", "\xce\x91", "&Beta;", "\xce\x92",
    "&Gamma;", "\xce\x93", "&Delta;", "\xce\x94", "&Epsilon;", "\xce\x95",
    "&Zeta;", "\xce\x96", "&Eta;", "\xce\x97", "&Theta;", "\xce\x98",
    "&Iota;", "\xce\x99", "&Kappa;", "\xce\x9a", "&Lambda;", "\xce\x9b",
    "&Mu;", "\xce\x9c", "&Nu;", "\xce\x9d", "&Xi;", "\xce\x9e",
    "&Omicron;", "\xce\x9f", "&Pi;", "\xce\xa0", "&Rho;", "\xce\xa1",
    "&Sigma;", "\xce\xa3", "&Tau;", "\xce\xa4", "&Upsilon;", "\xce\xa5",
    "&Phi;", "\xce\xa6", "&Chi;", "\xce\xa7", "&Psi;", "\xce\xa8",
    "&Omega;", "\xce\xa9", "&alpha;", "\xce\xb1", "&beta;", "\xce\xb2",
    "&gamma;", "\xce\xb3", "&delta;", "\xce\xb4", "&epsilon;", "\xce\xb5",
    "&zeta;", "\xce\xb6", "&eta;", "\xce\xb7", "&theta;", "\xce\xb8",
    "&iota;", "\xce\xb9", "&kappa;", "\xce\xba", "&lambda;", "\xce\xbb",
    "&mu;", "\xce\xbc", "&nu;", "\xce\xbd", "&xi;", "\xce\xbe",
    "&omicron;", "\xce\xbf", "&pi;", "\xcf\x80", "&rho;", "\xcf\x81",
    "&sigmaf;", "\xcf\x82", "&sigma;", "\xcf\x83", "&tau;", "\xcf\x84",
    "&upsilon;", "\xcf\x85", "&phi;", "\xcf\x86", "&chi;", "\xcf\x87",
    "&psi;", "\xcf\x88", "&omega;", "\xcf\x89", "&thetasym;", "\xcf\x91",
    "&upsih;", "\xcf\x92", "&piv;", "\xcf\x96", "&bull;", "\xe2\x80\xa2",
    "&hellip;", "\xe2\x80\xa6", "&prime;", "\xe2\x80\xb2", "&Prime;", "\xe2\x80\xb3",
    "&oline;", "\xe2\x80\xbe", "&frasl;", "\xe2\x81\x84", "&weierp;", "\xe2\x84\x98",
    "&image;", "\xe2\x84\x91", "&real;", "\xe2\x84\x9c", "&trade;", "\xe2\x84\xa2",
    "&alefsym;", "\xe2\x84\xb5", "&larr;", "\xe2\x86\x90", "&uarr;", "\xe2\x86\x91",
    "&rarr;", "\xe2\x86\x92", "&darr;", "\xe2\x86\x93", "&harr;", "\xe2\x86\x94",
    "&crarr;", "\xe2\x86\xb5", "&lArr;", "\xe2\x87\x90", "&uArr;", "\xe2\x87\x91",
    "&rArr;", "\xe2\x87\x92", "&dArr;", "\xe2\x87\x93", "&hArr;", "\xe2\x87\x94",
    "&forall;", "\xe2\x88\x80", "&part;", "\xe2\x88\x82", "&exist;", "\xe2\x88\x83",
    "&empty;", "\xe2\x88\x85", "&nabla;", "\xe2\x88\x87", "&isin;", "\xe2\x88\x88",
    "&notin;", "\xe2\x88\x89", "&ni;", "\xe2\x88\x8b", "&prod;", "\xe2\x88\x8f",
    "&sum;", "\xe2\x88\x91", "&minus;", "\xe2\x88\x92", "&lowast;", "\xe2\x88\x97",
    "&radic;", "\xe2\x88\x9a", "&prop;", "\xe2\x88\x9d", "&infin;", "\xe2\x88\x9e",
    "&ang;", "\xe2\x88\xa0", "&and;", "\xe2\x88\xa7", "&or;", "\xe2\x88\xa8",
    "&cap;", "\xe2\x88\xa9", "&cup;", "\xe2\x88\xaa", "&int;", "\xe2\x88\xab",
    "&there4;", "\xe2\x88\xb4", "&sim;", "\xe2\x88\xbc", "&cong;", "\xe2\x89\x85",
    "&asymp;", "\xe2\x89\x88", "&ne;", "\xe2\x89\xa0", "&equiv;", "\xe2\x89\xa1",
    "&le;", "\xe2\x89\xa4", "&ge;", "\xe2\x89\xa5", "&sub;", "\xe2\x8a\x82",
    "&sup;", "\xe2\x8a\x83", "&nsub;", "\xe2\x8a\x84", "&sube;", "\xe2\x8a\x86",
    "&supe;", "\xe2\x8a\x87", "&oplus;", "\xe2\x8a\x95", "&otimes;", "\xe2\x8a\x97",
    "&perp;", "\xe2\x8a\xa5", "&sdot;", "\xe2\x8b\x85", "&lceil;", "\xe2\x8c\x88",
    "&rceil;", "\xe2\x8c\x89", "&lfloor;", "\xe2\x8c\x8a", "&rfloor;", "\xe2\x8c\x8b",
    "&lang;", "\xe2\x8c\xa9", "&rang;", "\xe2\x8c\xaa", "&loz;", "\xe2\x97\x8a",
    "&spades;", "\xe2\x99\xa0", "&clubs;", "\xe2\x99\xa3", "&hearts;", "\xe2\x99\xa5",
    "&diams;", "\xe2\x99\xa6", "&OElig;", "\xc5\x92", "&oelig;", "\xc5\x93",
    "&Scaron;", "\xc5\xa0", "&scaron;", "\xc5\xa1", "&Yuml;", "\xc5\xb8",
    "&circ;", "\xcb\x86", "&tilde;", "\xcb\x9c", "&ensp;", "\xe2\x80\x82",
    "&emsp;", "\xe2\x80\x83", "&thinsp;", "\xe2\x80\x89", "&zwnj;", "\xe2\x80\x8c",
    "&zwj;", "\xe2\x80\x8d", "&lrm;", "\xe2\x80\x8e", "&rlm;", "\xe2\x80\x8f",
    "&ndash;", "\xe2\x80\x93", "&mdash;", "\xe2\x80\x94", "&lsquo;", "\xe2\x80\x98",
    "&rsquo;", "\xe2\x80\x99", "&sbquo;", "\xe2\x80\x9a", "&ldquo;", "\xe2\x80\x9c",
    "&rdquo;", "\xe2\x80\x9d", "&bdquo;", "\xe2\x80\x9e", "&dagger;", "\xe2\x80\xa0",
    "&Dagger;", "\xe2\x80\xa1", "&permil;", "\xe2\x80\xb0", "&lsaquo;", "\xe2\x80\xb9",
    "&rsaquo;", "\xe2\x80\xba", "&euro;", "\xe2\x82\xac",
    NULL
  };
  char *raw, *wp, buf[2], *tmp;
  int i, j, hit, num, tsiz;
  assert(html);
  CB_MALLOC(raw, strlen(html) * 3 + 1);
  wp = raw;
  while(*html != '\0'){
    if(*html == '&'){
      if(*(html + 1) == '#'){
        if(*(html + 2) == 'x' || *(html + 2) == 'X'){
          num = strtol(html + 3, NULL, 16);
        } else {
          num = atoi(html + 2);
        }
        buf[0] = num / 256;
        buf[1] = num % 256;
        if((tmp = est_uconv_out(buf, 2, &tsiz)) != NULL){
          for(j = 0; j < tsiz; j++){
            *wp = ((unsigned char *)tmp)[j];
            wp++;
          }
          free(tmp);
        }
        while(*html != ';' && *html != ' ' && *html != '\n' && *html != '\0'){
          html++;
        }
        if(*html == ';') html++;
      } else {
        hit = FALSE;
        for(i = 0; pairs[i] != NULL; i += 2){
          if(cbstrfwmatch(html, pairs[i])){
            wp += sprintf(wp, "%s", pairs[i+1]);
            html += strlen(pairs[i]);
            hit = TRUE;
            break;
          }
        }
        if(!hit){
          *wp = *html;
          wp++;
          html++;
        }
      }
    } else {
      *wp = *html;
      wp++;
      html++;
    }
  }
  *wp = '\0';
  return raw;
}

/* create a document object from MIME */
ESTDOC *est_doc_new_from_mime(const char *buf, int size,
                              const char *penc, int plang, int bcheck){
  ESTDOC *doc, *tdoc;
  CBMAP *attrs;
  const CBLIST *texts;
  CBLIST *parts, *lines;
  CBDATUM *datum;
  const char *key, *val, *bound, *part, *text, *line;
  char *body, *swap, numbuf[NUMBUFSIZ];
  int i, j, bsiz, psiz, ssiz, mht;
  assert(buf && size >= 0);
  doc = est_doc_new();
  attrs = cbmapopenex(MINIBNUM);
  body = cbmimebreak(buf, size, attrs, &bsiz);
  if((val = cbmapget(attrs, "subject", -1, NULL)) != NULL){
    est_doc_add_attr_mime(doc, ESTDATTRTITLE, val);
    if((val = est_doc_attr(doc, ESTDATTRTITLE)) != NULL) est_doc_add_hidden_text(doc, val);
  }
  if((val = cbmapget(attrs, "from", -1, NULL)) != NULL)
    est_doc_add_attr_mime(doc, ESTDATTRAUTHOR, val);
  if((val = cbmapget(attrs, "date", -1, NULL)) != NULL){
    est_doc_add_attr_mime(doc, ESTDATTRCDATE, val);
    est_doc_add_attr_mime(doc, ESTDATTRMDATE, val);
  }
  est_doc_add_attr(doc, ESTDATTRTYPE, "message/rfc822");
  sprintf(numbuf, "%d", size);
  est_doc_add_attr(doc, ESTDATTRSIZE, numbuf);
  cbmapiterinit(attrs);
  while((key = cbmapiternext(attrs, NULL)) != NULL){
    if((key[0] >= 'A' && key[0] <= 'Z') || key[0] == '@' || key[0] == '_') continue;
    val = cbmapiterval(key, NULL);
    est_doc_add_attr_mime(doc, key, val);
  }
  if((key = cbmapget(attrs, "TYPE", -1, NULL)) != NULL && cbstrfwimatch(key, "multipart/")){
    mht = cbstrfwimatch(key, "multipart/related");
    if((bound = cbmapget(attrs, "BOUNDARY", -1, NULL)) != NULL){
      parts = cbmimeparts(body, bsiz, bound);
      for(i = 0; i < CB_LISTNUM(parts) && i < 8; i++){
        part = CB_LISTVAL2(parts, i, psiz);
        if((tdoc = est_doc_new_from_mime(part, psiz, penc, plang, bcheck)) != NULL){
          if(mht){
            if((text = est_doc_attr(tdoc, ESTDATTRTITLE)) != NULL)
              est_doc_add_attr(doc, ESTDATTRTITLE, text);
            if((text = est_doc_attr(tdoc, ESTDATTRAUTHOR)) != NULL)
              est_doc_add_attr(doc, ESTDATTRAUTHOR, text);
          }
          texts = est_doc_texts(tdoc);
          for(j = 0; j < CB_LISTNUM(texts); j++){
            text = CB_LISTVAL(texts, j);
            est_doc_add_text(doc, text);
          }
          est_doc_delete(tdoc);
        }
      }
      CB_LISTCLOSE(parts);
    }
  } else {
    key = cbmapget(attrs, "content-transfer-encoding", -1, NULL);
    if(key && cbstrfwimatch(key, "base64")){
      swap = cbbasedecode(body, &ssiz);
      free(body);
      body = swap;
      bsiz = ssiz;
    } else if(key && cbstrfwimatch(key, "quoted-printable")){
      swap = cbquotedecode(body, &ssiz);
      free(body);
      body = swap;
      bsiz = ssiz;
    }
    key = cbmapget(attrs, "content-encoding", -1, NULL);
    if(key && (cbstrfwimatch(key, "x-gzip") || cbstrfwimatch(key, "gzip")) &&
       (swap = cbgzdecode(body, bsiz, &ssiz)) != NULL){
      free(body);
      body = swap;
      bsiz = ssiz;
    } else if(key && (cbstrfwimatch(key, "x-deflate") || cbstrfwimatch(key, "deflate")) &&
              (swap = cbinflate(body, bsiz, &ssiz)) != NULL){
      free(body);
      body = swap;
      bsiz = ssiz;
    }
    if(!(key = cbmapget(attrs, "TYPE", -1, NULL)) || cbstrfwimatch(key, "text/plain")){
      if(!bcheck || !est_check_binary(body, bsiz)){
        if(penc && (swap = est_iconv(body, bsiz, penc, "UTF-8", &ssiz, NULL)) != NULL){
          free(body);
          body = swap;
          bsiz = ssiz;
        } else if((key = cbmapget(attrs, "CHARSET", -1, NULL)) != NULL &&
                  (swap = est_iconv(body, bsiz, key, "UTF-8", &ssiz, NULL)) != NULL){
          free(body);
          body = swap;
          bsiz = ssiz;
        }
        lines = cbsplit(body, bsiz, "\n");
        CB_DATUMOPEN(datum);
        for(i = 0; i < CB_LISTNUM(lines); i++){
          line = CB_LISTVAL(lines, i);
          while(*line == ' ' || *line == '>' || *line == '|' || *line == '\t' || *line == '\r'){
            line++;
          }
          if(line[0] == '\0'){
            est_doc_add_text(doc, CB_DATUMPTR(datum));
            CB_DATUMSETSIZE(datum, 0);
          } else {
            CB_DATUMCAT(datum, " ", 1);
            CB_DATUMCAT(datum, line, (ssize_t)strlen(line));
          }
        }
        est_doc_add_text(doc, CB_DATUMPTR(datum));
        CB_DATUMCLOSE(datum);
        CB_LISTCLOSE(lines);
      }
    } else if(cbstrfwimatch(key, "text/html") || cbstrfwimatch(key, "application/xhtml+xml")){
      if((tdoc = est_doc_new_from_html(body, bsiz, penc, plang, bcheck)) != NULL){
        if((text = est_doc_attr(tdoc, ESTDATTRTITLE)) != NULL){
          if(!est_doc_attr(doc, ESTDATTRTITLE)) est_doc_add_attr(doc, ESTDATTRTITLE, text);
          est_doc_add_text(doc, text);
        }
        if((text = est_doc_attr(tdoc, ESTDATTRAUTHOR)) != NULL){
          if(!est_doc_attr(doc, ESTDATTRAUTHOR)) est_doc_add_attr(doc, ESTDATTRAUTHOR, text);
          est_doc_add_text(doc, text);
        }
        texts = est_doc_texts(tdoc);
        for(i = 0; i < CB_LISTNUM(texts); i++){
          text = CB_LISTVAL(texts, i);
          est_doc_add_text(doc, text);
        }
        est_doc_delete(tdoc);
      }
    } else if(cbstrfwimatch(key, "message/rfc822")){
      if((tdoc = est_doc_new_from_mime(body, bsiz, penc, plang, bcheck)) != NULL){
        if((text = est_doc_attr(tdoc, ESTDATTRTITLE)) != NULL){
          if(!est_doc_attr(doc, ESTDATTRTITLE)) est_doc_add_attr(doc, ESTDATTRTITLE, text);
          est_doc_add_text(doc, text);
        }
        if((text = est_doc_attr(tdoc, ESTDATTRAUTHOR)) != NULL){
          if(!est_doc_attr(doc, ESTDATTRAUTHOR)) est_doc_add_attr(doc, ESTDATTRAUTHOR, text);
          est_doc_add_text(doc, text);
        }
        texts = est_doc_texts(tdoc);
        for(i = 0; i < CB_LISTNUM(texts); i++){
          text = CB_LISTVAL(texts, i);
          est_doc_add_text(doc, text);
        }
        est_doc_delete(tdoc);
      }
    } else if(cbstrfwimatch(key, "text/")){
      if((tdoc = est_doc_new_from_text(body, bsiz, penc, plang, bcheck)) != NULL){
        texts = est_doc_texts(tdoc);
        for(i = 0; i < CB_LISTNUM(texts); i++){
          text = CB_LISTVAL(texts, i);
          est_doc_add_text(doc, text);
        }
        est_doc_delete(tdoc);
      }
    }
  }
  free(body);
  cbmapclose(attrs);
  return doc;
}

/* set mime value as an attribute of a document */
static void est_doc_add_attr_mime(ESTDOC *doc, const char *name, const char *value){
  char enc[64], *ebuf, *rbuf;
  assert(doc && name && value);
  ebuf = cbmimedecode(value, enc);
  if((rbuf = est_iconv(ebuf, -1, enc, "UTF-8", NULL, NULL)) != NULL){
    est_doc_add_attr(doc, name, rbuf);
    free(rbuf);
  }
  free(ebuf);
}
