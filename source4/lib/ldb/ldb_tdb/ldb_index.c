/* 
   ldb database library

   Copyright (C) Andrew Tridgell  2004

     ** NOTE! The following LGPL license applies to the ldb
     ** library. This does NOT imply that all of Samba is released
     ** under the LGPL
   
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
 *  Name: ldb
 *
 *  Component: ldb tdb backend - indexing
 *
 *  Description: indexing routines for ldb tdb backend
 *
 *  Author: Andrew Tridgell
 */

#include "includes.h"

struct dn_list {
	unsigned int count;
	char **dn;
};

/*
  free a struct dn_list
*/
static void dn_list_free(struct dn_list *list)
{
	int i;
	for (i=0;i<list->count;i++) {
		free(list->dn[i]);
	}
	if (list->dn) free(list->dn);
}

/*
  return the dn key to be used for an index
  caller frees
*/
static char *ldb_dn_key(const char *attr, const struct ldb_val *value)
{
	char *ret = NULL;

	if (ldb_should_b64_encode(value)) {
		char *vstr = ldb_base64_encode(value->data, value->length);
		if (!vstr) return NULL;
		asprintf(&ret, "@INDEX:%s::%s", attr, vstr);
		free(vstr);
		return ret;
	}

	asprintf(&ret, "@INDEX:%s:%s", attr, (char *)value->data);
	return ret;
}

/*
  see if a attribute value is in the list of indexed attributes
*/
static int ldb_msg_find_idx(const struct ldb_message *msg, const char *attr)
{
	int i;
	for (i=0;i<msg->num_elements;i++) {
		if (strcmp(msg->elements[i].name, "@IDXATTR") == 0 &&
		    strcmp((char *)msg->elements[i].value.data, attr) == 0) {
			return i;
		}
	}
	return -1;
}

/*
  return a list of dn's that might match a simple indexed search or
 */
static int ltdb_index_dn_simple(struct ldb_context *ldb, 
				struct ldb_parse_tree *tree,
				const struct ldb_message *index_list,
				struct dn_list *list)
{
	char *dn = NULL;
	int ret, i;
	struct ldb_message msg;

	list->count = 0;
	list->dn = NULL;

	/*
	  if the value is a wildcard then we can't do a match via indexing
	*/
	if (ltdb_has_wildcard(&tree->u.simple.value)) {
		return -1;
	}

	/* if the attribute isn't in the list of indexed attributes then
	   this node needs a full search */
	if (ldb_msg_find_idx(index_list, tree->u.simple.attr) == -1) {
		return -1;
	}

	/* the attribute is indexed. Pull the list of DNs that match the 
	   search criterion */
	dn = ldb_dn_key(tree->u.simple.attr, &tree->u.simple.value);
	if (!dn) return -1;

	ret = ltdb_search_dn1(ldb, dn, &msg);
	free(dn);
	if (ret == 0 || ret == -1) {
		return ret;
	}

	list->dn = malloc_array_p(char *, msg.num_elements);
	if (!list->dn) {
		ltdb_search_dn1_free(ldb, &msg);
	}

	for (i=0;i<msg.num_elements;i++) {
		if (strcmp(msg.elements[i].name, "@IDX") != 0) {
			continue;
		}
		list->dn[list->count] = 
			strdup((char *)msg.elements[i].value.data);
		if (!list->dn[list->count]) {
			dn_list_free(list);
			ltdb_search_dn1_free(ldb, &msg);
			return -1;
		}
		list->count++;
	}

	ltdb_search_dn1_free(ldb, &msg);

	qsort(list->dn, list->count, sizeof(char *), (comparison_fn_t) strcmp);

	return 1;
}

/*
  list intersection
  list = list & list2
  relies on the lists being sorted
*/
static int list_intersect(struct dn_list *list, const struct dn_list *list2)
{
	struct dn_list list3;
	int i;

	if (list->count == 0 || list2->count == 0) {
		/* 0 & X == 0 */
		dn_list_free(list);
		return 0;
	}

	list3.dn = malloc_array_p(char *, list->count);
	if (!list3.dn) {
		dn_list_free(list);
		return -1;
	}
	list3.count = 0;

	for (i=0;i<list->count;i++) {
		if (list_find(list->dn[i], list2->dn, list2->count, 
			      sizeof(char *), (comparison_fn_t)strcmp) != -1) {
			list3.dn[list3.count] = list->dn[i];
			list3.count++;
		} else {
			free(list->dn[i]);
		}		
	}

	free(list->dn);
	list->dn = list3.dn;
	list->count = list3.count;

	return 0;
}


/*
  list union
  list = list | list2
  relies on the lists being sorted
*/
static int list_union(struct dn_list *list, const struct dn_list *list2)
{
	int i;
	char **d;
	unsigned int count = list->count;

	if (list->count == 0 && list2->count == 0) {
		/* 0 | 0 == 0 */
		dn_list_free(list);
		return 0;
	}

	d = realloc_p(list->dn, char *, list->count + list2->count);
	if (!d) {
		dn_list_free(list);
		return -1;
	}
	list->dn = d;

	for (i=0;i<list2->count;i++) {
		if (list_find(list2->dn[i], list->dn, count, 
			      sizeof(char *), (comparison_fn_t)strcmp) == -1) {
			list->dn[list->count] = strdup(list2->dn[i]);
			if (!list->dn[list->count]) {
				dn_list_free(list);
				return -1;
			}
			list->count++;
		}		
	}

	if (list->count != count) {
		qsort(list->dn, list->count, sizeof(char *), (comparison_fn_t)strcmp);
	}

	return 0;
}

static int ltdb_index_dn(struct ldb_context *ldb, 
			 struct ldb_parse_tree *tree,
			 const struct ldb_message *index_list,
			 struct dn_list *list);


/*
  OR two index results
 */
static int ltdb_index_dn_or(struct ldb_context *ldb, 
			    struct ldb_parse_tree *tree,
			    const struct ldb_message *index_list,
			    struct dn_list *list)
{
	int ret, i;
	
	ret = -1;
	list->dn = NULL;
	list->count = 0;

	for (i=0;i<tree->u.list.num_elements;i++) {
		struct dn_list list2;
		int v;
		v = ltdb_index_dn(ldb, tree->u.list.elements[i], index_list, &list2);

		if (v == 0) {
			/* 0 || X == X */
			if (ret == -1) {
				ret = 0;
			}
			continue;
		}

		if (v == -1) {
			/* 1 || X == 1 */
			dn_list_free(list);
			return -1;
		}

		if (ret == -1) {
			ret = 1;
			*list = list2;
		} else {
			if (list_union(list, &list2) == -1) {
				dn_list_free(&list2);
				return -1;
			}
			dn_list_free(&list2);
		}
	}

	if (list->count == 0) {
		dn_list_free(list);
		return 0;
	}

	return ret;
}


/*
  NOT an index results
 */
static int ltdb_index_dn_not(struct ldb_context *ldb, 
			     struct ldb_parse_tree *tree,
			     const struct ldb_message *index_list,
			     struct dn_list *list)
{
	/* the only way to do an indexed not would be if we could
	   negate the not via another not or if we knew the total
	   number of database elements so we could know that the
	   existing expression covered the whole database. 
	   
	   instead, we just give up, and rely on a full index scan
	   (unless an outer & manages to reduce the list)
	*/
	return -1;
}

/*
  AND two index results
 */
static int ltdb_index_dn_and(struct ldb_context *ldb, 
			     struct ldb_parse_tree *tree,
			     const struct ldb_message *index_list,
			     struct dn_list *list)
{
	int ret, i;
	
	ret = -1;
	list->dn = NULL;
	list->count = 0;

	for (i=0;i<tree->u.list.num_elements;i++) {
		struct dn_list list2;
		int v;
		v = ltdb_index_dn(ldb, tree->u.list.elements[i], index_list, &list2);

		if (v == 0) {
			/* 0 && X == 0 */
			dn_list_free(list);
			return 0;
		}

		if (v == -1) {
			continue;
		}

		if (ret == -1) {
			ret = 1;
			*list = list2;
		} else {
			if (list_intersect(list, &list2) == -1) {
				dn_list_free(&list2);
				return -1;
			}
			dn_list_free(&list2);
		}

		if (list->count == 0) {
			if (list->dn) free(list->dn);
			return 0;
		}
	}

	return ret;
}

/*
  return a list of dn's that might match a indexed search or
  -1 if an error. return 0 for no matches, or 1 for matches
 */
static int ltdb_index_dn(struct ldb_context *ldb, 
			 struct ldb_parse_tree *tree,
			 const struct ldb_message *index_list,
			 struct dn_list *list)
{
	int ret;

	switch (tree->operation) {
	case LDB_OP_SIMPLE:
		ret = ltdb_index_dn_simple(ldb, tree, index_list, list);
		break;

	case LDB_OP_AND:
		ret = ltdb_index_dn_and(ldb, tree, index_list, list);
		break;

	case LDB_OP_OR:
		ret = ltdb_index_dn_or(ldb, tree, index_list, list);
		break;

	case LDB_OP_NOT:
		ret = ltdb_index_dn_not(ldb, tree, index_list, list);
		break;
	}

	return ret;
}

/*
  filter a candidate dn_list from an indexed search into a set of results
  extracting just the given attributes
*/
static int ldb_index_filter(struct ldb_context *ldb, struct ldb_parse_tree *tree,
			    const char *base,
			    enum ldb_scope scope,
			    const struct dn_list *dn_list, 
			    const char *attrs[], struct ldb_message ***res)
{
	int i;
	unsigned int count = 0;

	for (i=0;i<dn_list->count;i++) {
		struct ldb_message msg;
		int ret;
		ret = ltdb_search_dn1(ldb, dn_list->dn[i], &msg);
		if (ret == 0) {
			/* the record has disappeared? yes, this can happen */
			continue;
		}

		if (ret == -1) {
			/* an internal error */
			return -1;
		}

		if (ldb_message_match(ldb, &msg, tree, base, scope) == 1) {
			ret = ltdb_add_attr_results(ldb, &msg, attrs, &count, res);
		}
		ltdb_search_dn1_free(ldb, &msg);
		if (ret != 0) {
			return -1;
		}
	}

	return count;
}

/*
  search the database with a LDAP-like expression using indexes
  returns -1 if an indexed search is not possible, in which
  case the caller should call ltdb_search_full() 
*/
int ltdb_search_indexed(struct ldb_context *ldb, 
			const char *base,
			enum ldb_scope scope,
			struct ldb_parse_tree *tree,
			const char *attrs[], struct ldb_message ***res)
{
	struct ldb_message index_list;
	struct dn_list dn_list;
	int ret;

	/* find the list of indexed fields */	
	ret = ltdb_search_dn1(ldb, "@INDEXLIST", &index_list);
	if (ret != 1) {
		/* no index list? must do full search */
		return -1;
	}

	ret = ltdb_index_dn(ldb, tree, &index_list, &dn_list);
	ltdb_search_dn1_free(ldb, &index_list);

	if (ret == 1) {
		/* we've got a candidate list - now filter by the full tree
		   and extract the needed attributes */
		ret = ldb_index_filter(ldb, tree, base, scope, &dn_list, 
				       attrs, res);
		dn_list_free(&dn_list);
	}

	return ret;
}

/*
  add an index entry for one message element
*/
static int ltdb_index_add1(struct ldb_context *ldb, const char *dn, 
			   struct ldb_message_element *el)
{
	struct ldb_message msg;
	char *dn_key;
	int ret;
	struct ldb_message_element *el2;

	dn_key = ldb_dn_key(el->name, &el->value);
	if (!dn_key) {
		return -1;
	}

	ret = ltdb_search_dn1(ldb, dn_key, &msg);
	if (ret == -1) {
		free(dn_key);
		return -1;
	}

	if (ret == 0) {
		msg.dn = dn_key;
		msg.num_elements = 0;
		msg.elements = NULL;
		msg.private = NULL;
	}

	/* add another entry */
	el2 = realloc_p(msg.elements, struct ldb_message_element, msg.num_elements+1);
	if (!el2) {
		if (ret == 1) {
			ltdb_search_dn1_free(ldb, &msg);
		}
		free(dn_key);
		return -1;
	}

	msg.elements = el2;
	msg.elements[msg.num_elements].name = "@IDX";
	msg.elements[msg.num_elements].value.length = strlen(dn);
	msg.elements[msg.num_elements].value.data = dn;
	msg.num_elements++;

	ret = ltdb_store(ldb, &msg, TDB_REPLACE);

	if (msg.num_elements == 1) {
		free(msg.elements);
	} else {
		ltdb_search_dn1_free(ldb, &msg);
	}

	return ret;
}

/*
  add the index entries for a new record
  return -1 on failure
*/
int ltdb_index_add(struct ldb_context *ldb, const struct ldb_message *msg)
{
	int ret, i;
	struct ldb_message index_list;

	/* find the list of indexed fields */	
	ret = ltdb_search_dn1(ldb, "@INDEXLIST", &index_list);
	if (ret != 1) {
		/* no indexed fields or an error */
		return ret;
	}

	for (i=0;i<msg->num_elements;i++) {
		ret = ldb_msg_find_idx(&index_list, msg->elements[i].name);
		if (ret == -1) {
			continue;
		}
		ret = ltdb_index_add1(ldb, msg->dn, &msg->elements[i]);
		if (ret == -1) {
			ltdb_search_dn1_free(ldb, &index_list);
			return -1;
		}
	}

	return 0;
}


/*
  delete an index entry for one message element
*/
static int ltdb_index_del1(struct ldb_context *ldb, const char *dn, 
			   struct ldb_message_element *el)
{
	struct ldb_message msg;
	char *dn_key;
	int ret, i;

	dn_key = ldb_dn_key(el->name, &el->value);
	if (!dn_key) {
		return -1;
	}

	ret = ltdb_search_dn1(ldb, dn_key, &msg);
	if (ret == -1) {
		free(dn_key);
		return -1;
	}

	if (ret == 0) {
		/* it wasn't indexed. Did we have an earlier error? If we did then
		   its gone now */
		ltdb_search_dn1_free(ldb, &msg);
		return 0;
	}

	i = ldb_msg_find_idx(&msg, dn);
	if (i == -1) {
		/* it ain't there. hmmm */
		ltdb_search_dn1_free(ldb, &msg);
		return 0;
	}

	if (i != msg.num_elements - 1) {
		memmove(&msg.elements[i], &msg.elements[i+1], sizeof(msg.elements[i]));
	}
	msg.num_elements--;

	if (msg.num_elements == 0) {
		ret = ltdb_delete_noindex(ldb, dn_key);
	} else {
		ret = ltdb_store(ldb, &msg, TDB_REPLACE);
	}

	ltdb_search_dn1_free(ldb, &msg);

	return ret;
}

/*
  delete the index entries for a record
  return -1 on failure
*/
int ltdb_index_del(struct ldb_context *ldb, const struct ldb_message *msg)
{
	int ret, i;
	struct ldb_message index_list;

	/* find the list of indexed fields */	
	ret = ltdb_search_dn1(ldb, "@INDEXLIST", &index_list);
	if (ret != 1) {
		/* no indexed fields or an error */
		return ret;
	}

	for (i=0;i<msg->num_elements;i++) {
		ret = ldb_msg_find_idx(&index_list, msg->elements[i].name);
		if (ret == -1) {
			continue;
		}
		ret = ltdb_index_del1(ldb, msg->dn, &msg->elements[i]);
		if (ret == -1) {
			ltdb_search_dn1_free(ldb, &index_list);
			return -1;
		}
	}

	return 0;
}
