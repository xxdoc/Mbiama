/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 *
 * @file svn_branch_nested.h
 * @brief Nested branches and subbranch-root elements
 *
 * @since New in 1.10.
 */

#ifndef SVN_BRANCH_NESTED_H
#define SVN_BRANCH_NESTED_H

#include <apr_pools.h>

#include "svn_types.h"

#include "private/svn_branch.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*
 */
void
svn_branch_get_outer_branch_and_eid(svn_branch_state_t **outer_branch_p,
                                    int *outer_eid_p,
                                    const svn_branch_state_t *branch,
                                    apr_pool_t *scratch_pool);

/* Return the root repos-relpath of BRANCH.
 *
 * ### TODO: Clarify sequencing requirements.
 */
const char *
svn_branch_get_root_rrpath(const svn_branch_state_t *branch,
                           apr_pool_t *result_pool);

/* Return the repos-relpath of element EID in BRANCH.
 *
 * If the element EID does not currently exist in BRANCH, return NULL.
 *
 * ### TODO: Clarify sequencing requirements.
 */
const char *
svn_branch_get_rrpath_by_eid(const svn_branch_state_t *branch,
                             int eid,
                             apr_pool_t *result_pool);

/* Return the EID for the repos-relpath RRPATH in BRANCH.
 *
 * If no element of BRANCH is at this path, return -1.
 *
 * ### TODO: Clarify sequencing requirements.
 */
int
svn_branch_get_eid_by_rrpath(svn_branch_state_t *branch,
                             const char *rrpath,
                             apr_pool_t *scratch_pool);

/* Find the (deepest) branch of which the path RELPATH is either the root
 * path or a normal, non-sub-branch path. An element need not exist at
 * RELPATH.
 *
 * Set *BRANCH_P to the deepest branch within ROOT_BRANCH (recursively,
 * including itself) that contains the path RELPATH.
 *
 * If EID_P is not null then set *EID_P to the element id of RELPATH in
 * *BRANCH_P, or to -1 if no element exists at RELPATH in that branch.
 *
 * If RELPATH is not within any branch in ROOT_BRANCH, set *BRANCH_P to
 * NULL and (if EID_P is not null) *EID_P to -1.
 *
 * ### TODO: Clarify sequencing requirements.
 */
void
svn_branch_find_nested_branch_element_by_relpath(
                                svn_branch_state_t **branch_p,
                                int *eid_p,
                                svn_branch_state_t *root_branch,
                                const char *relpath,
                                apr_pool_t *scratch_pool);

/* Set *EL_REV_P to the el-rev-id of the element at relative path RELPATH
 * anywhere in or under branch BRANCH_ID in revision REVNUM in REPOS.
 *
 * If there is no element there, set *EL_REV_P to point to an id in which
 * the BRANCH field is the nearest enclosing branch of RRPATH and the EID
 * field is -1.
 *
 * Allocate *EL_REV_P (but not the branch object that it refers to) in
 * RESULT_POOL.
 *
 * ### TODO: Clarify sequencing requirements.
 */
svn_error_t *
svn_branch_repos_find_el_rev_by_path_rev(svn_branch_el_rev_id_t **el_rev_p,
                                const svn_branch_repos_t *repos,
                                svn_revnum_t revnum,
                                const char *branch_id,
                                const char *relpath,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool);

/* Return an array of pointers to the branches that are immediate
 * sub-branches of BRANCH.
 */
apr_array_header_t *
svn_branch_get_immediate_subbranches(svn_branch_state_t *branch,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool);

/* Return the subbranch rooted at BRANCH:EID, or NULL if that is
 * not a subbranch root.
 */
svn_branch_state_t *
svn_branch_get_subbranch_at_eid(svn_branch_state_t *branch,
                                int eid,
                                apr_pool_t *scratch_pool);

/* A subtree of a branch, including any nested branches.
 */
typedef struct svn_branch_subtree_t
{
  svn_branch_rev_bid_t *predecessor;

  /* EID -> svn_branch_el_rev_content_t mapping. */
  svn_element_tree_t *tree;

  /* Subbranches to be included: each subbranch-root element in E_MAP
     should be mapped here.

     A mapping of (int)EID -> (svn_branch_subtree_t *). */
  apr_hash_t *subbranches;
} svn_branch_subtree_t;

/* Create an empty subtree (no elements populated, not even ROOT_EID).
 *
 * The result contains a *shallow* copy of E_MAP, or a new empty mapping
 * if E_MAP is null.
 */
svn_branch_subtree_t *
svn_branch_subtree_create(apr_hash_t *e_map,
                          int root_eid,
                          apr_pool_t *result_pool);

/* Return the subtree of BRANCH rooted at EID.
 * Recursive: includes subbranches.
 *
 * The result is limited by the lifetime of BRANCH. It includes a shallow
 * copy of the element maps in BRANCH and its subbranches: the hash tables
 * are duplicated but the keys and values (element content data) are not.
 * It assumes that modifications on a svn_branch_state_t treat element
 * map keys and values as immutable -- which they do.
 */
svn_branch_subtree_t *
svn_branch_get_subtree(svn_branch_state_t *branch,
                       int eid,
                       apr_pool_t *result_pool);

/* Return the subbranch rooted at SUBTREE:EID, or NULL if that is
 * not a subbranch root. */
svn_branch_subtree_t *
svn_branch_subtree_get_subbranch_at_eid(svn_branch_subtree_t *subtree,
                                        int eid,
                                        apr_pool_t *result_pool);

/* Instantiate elements in a branch.
 *
 * In TO_BRANCH, instantiate (or alter, if existing) each element of
 * ELEMENTS, each with its given tree structure (parent, name) and payload.
 *
 * Also branch the subbranches in ELEMENTS, creating corresponding new
 * subbranches in TO_BRANCH, recursively.
 */
svn_error_t *
svn_branch_instantiate_elements_r(svn_branch_state_t *to_branch,
                                  svn_branch_subtree_t elements,
                                  apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_BRANCH_NESTED_H */