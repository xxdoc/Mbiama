/*
 * copy.c:  copy/move wrappers around wc 'copy' functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include <string.h>
#include <assert.h>
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_opt.h"
#include "svn_time.h"

#include "client.h"


/*** Code. ***/

/*
 * if (not exist src_path)
 *   return ERR_BAD_SRC error
 *
 * if (exist dst_path)
 *   {
 *     if (dst_path is directory)
 *       copy src_path into dst_path as basename (src_path)
 *     else
 *       return ERR_OBSTRUCTION error
 *   }
 * else
 *   copy src_path into parent_of_dst_path as basename (dst_path)
 *
 * if (this is a move)
 *   delete src_path
 */


/* Copy SRC_PATH into DST_PATH as DST_BASENAME, deleting SRC_PATH
   afterwards if IS_MOVE is TRUE.  Use POOL for all necessary
   allocations.

   ### 838 OPTIONAL_ADM_ACCESS is an access baton with a write lock for the
   ### parent of DST_PATH. This parameter should be removed when issue 838
   ### stops using svn_client_copy.
*/
static svn_error_t *
wc_to_wc_copy (const char *src_path,
               const char *dst_path,
               svn_wc_adm_access_t *optional_adm_access,
               svn_boolean_t is_move,
               svn_boolean_t force,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  svn_node_kind_t src_kind, dst_kind;
  const char *dst_parent, *base_name;
  svn_wc_adm_access_t *adm_access, *src_access;

  /* Verify that SRC_PATH exists. */
  SVN_ERR (svn_io_check_path (src_path, &src_kind, pool));
  if (src_kind == svn_node_none)
    return svn_error_createf (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                              "path `%s' does not exist.", src_path);

  /* If DST_PATH does not exist, then its basename will become a new
     file or dir added to its parent (possibly an implicit '.').  If
     DST_PATH is a dir, then SRC_PATH's basename will become a new
     file or dir within DST_PATH itself.  Else if it's a file, just
     error out. */
  SVN_ERR (svn_io_check_path (dst_path, &dst_kind, pool));
  if (dst_kind == svn_node_none)
    {
      svn_path_split (dst_path, &dst_parent, &base_name, pool);
    }
  else if (dst_kind == svn_node_dir)
    {
      svn_path_split (src_path, NULL, &base_name, pool);
      dst_parent = dst_path;
    }
  else
    return svn_error_createf (SVN_ERR_ENTRY_EXISTS, NULL,
                              "file `%s' already exists.", dst_path);

  if (is_move)
    {
      /* We don't handle optional_adm_access here.  The merge code that
         sets it calls svn_client_copy rather than svn_client_move */
      const char *src_parent;
      assert (! optional_adm_access);

      svn_path_split (src_path, &src_parent, NULL, pool);

      SVN_ERR (svn_wc_adm_open (&src_access, NULL, src_parent, TRUE,
                                src_kind == svn_node_dir,
                                pool));

      /* Need to avoid attempting to open the same dir twice when source
         and destination overlap. */
      if (strcmp (src_parent, dst_parent) == 0)
        {
          adm_access = src_access;
        }
      else 
        {
          const char *src_parent_abs, *dst_parent_abs;

          SVN_ERR (svn_path_get_absolute (&src_parent_abs, src_parent, pool));
          SVN_ERR (svn_path_get_absolute (&dst_parent_abs, dst_parent, pool));

          if ((src_kind == svn_node_dir)
              && (svn_path_is_child (src_parent_abs, dst_parent_abs, pool)))
            {
              SVN_ERR (svn_wc_adm_retrieve (&adm_access, src_access,
                                            dst_parent, pool));
            }
          else
            {
              SVN_ERR (svn_wc_adm_open (&adm_access, NULL, dst_parent,
                                        TRUE, FALSE, pool));
            }
        }

      if (!force)
        /* Ensure there are no "awkward" files. */
        SVN_ERR_W (svn_client__can_delete (src_path, src_access, ctx, pool),
                   "Pass --force to override this restriction");
    }
  else if (! optional_adm_access)
    SVN_ERR (svn_wc_adm_open (&adm_access, NULL, dst_parent, TRUE, FALSE,
                              pool));
  else
    adm_access = optional_adm_access;
                              
  /* Perform the copy and (optionally) delete. */

  /* ### If this is not a move, we won't have locked the source, so we
     ### won't detect any outstanding locks. If the source is locked and
     ### requires cleanup should we abort the copy? */

  SVN_ERR (svn_wc_copy (src_path, adm_access, base_name,
                        ctx->cancel_func, ctx->cancel_baton,
                        ctx->notify_func, ctx->notify_baton, pool));


  if (is_move)
    {
      SVN_ERR (svn_wc_delete (src_path, src_access,
                              ctx->cancel_func, ctx->cancel_baton,
                              ctx->notify_func, ctx->notify_baton, pool));

      if (adm_access != src_access)
        SVN_ERR (svn_wc_adm_close (adm_access));
      SVN_ERR (svn_wc_adm_close (src_access));
    }
  else if (! optional_adm_access)
    SVN_ERR (svn_wc_adm_close (adm_access));

  return SVN_NO_ERROR;
}


static svn_error_t *
repos_to_repos_copy (svn_client_commit_info_t **commit_info,
                     const char *src_url, 
                     const svn_opt_revision_t *src_revision, 
                     const char *dst_url, 
                     svn_client_ctx_t *ctx,
                     const char *message,
                     svn_boolean_t is_move,
                     apr_pool_t *pool)
{
  const char *top_url, *src_rel, *dst_rel, *base_name, *unused;
  apr_array_header_t *src_pieces = NULL, *dst_pieces = NULL;
  svn_revnum_t youngest;
  void *ra_baton, *sess;
  svn_ra_plugin_t *ra_lib;
  svn_node_kind_t src_kind, dst_kind;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *root_baton, *baton;
  void **batons;
  int i = 0;
  svn_revnum_t committed_rev = SVN_INVALID_REVNUM;
  const char *committed_date = NULL;
  const char *committed_author = NULL;
  svn_revnum_t src_revnum;
  const char *piece, *telepath;
  const char *auth_dir;

  /* ### TODO:  Currently, this function will violate the depth-first
     rule of editors when doing a move of something up into one of its
     grandparent directories, such as:

        svn mv http://server/repos/dir1/dir2/file http://server/repos/dir1

     While it seems to work just fine, we might want to evaluate this
     from a purely "correctness" standpoint.
  */

  /* We have to open our session to the longest path common to both
     SRC_URL and DST_URL in the repository so we can do existence
     checks on both paths, and so we can operate on both paths in the
     case of a move. */
  top_url = svn_path_get_longest_ancestor (src_url, dst_url, pool);

  /* Special edge-case!  (issue #683)  If you're resurrecting a
     deleted item like this:  'svn cp -rN src_URL dst_URL', then it's
     possible for src_URL == dst_URL == top_url.  In this situation,
     we want to open an RA session to the *parent* of all three. */
  if (! strcmp (src_url, dst_url))
    top_url = svn_path_dirname (top_url, pool);

  /* Get the portions of the SRC and DST URLs that are relative to
     TOP_URL. */
  src_rel = svn_path_is_child (top_url, src_url, pool);
  if (src_rel)
    {
      src_rel = svn_path_uri_decode (src_rel, pool);
      src_pieces = svn_path_decompose (src_rel, pool);
      if ((! src_pieces) || (! src_pieces->nelts))
        return svn_error_createf 
          (SVN_ERR_WC_PATH_NOT_FOUND, NULL,
           "error decomposing relative path `%s'", src_rel);
    }
  else
    src_rel = "";

  dst_rel = svn_path_is_child (top_url, dst_url, pool);
  if (dst_rel)
    {
      dst_rel = svn_path_uri_decode (dst_rel, pool);
      dst_pieces = svn_path_decompose (dst_rel, pool);
      if ((! dst_pieces) || (! dst_pieces->nelts))
        return svn_error_createf 
          (SVN_ERR_WC_PATH_NOT_FOUND, NULL,
           "error decomposing relative path `%s'", dst_rel);
    }
  else
    dst_rel = "";

  /* Allocate room for the root baton, the pieces of the
     source's or destination's path, and the destination itself. */
  {
    int num, num2;
    num = src_pieces ? src_pieces->nelts : 0;
    if (((num2 = (dst_pieces ? dst_pieces->nelts : 0))) > num)
      num = num2;

    batons = apr_palloc (pool, sizeof (void *) * (num + 2));
  }

  /* Get the RA vtable that matches URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, top_url, pool));

  SVN_ERR (svn_client__dir_if_wc (&auth_dir, "", pool));

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files or store the auth data. */
  SVN_ERR (svn_client__open_ra_session (&sess, ra_lib, top_url,
                                        auth_dir,
                                        NULL, NULL, FALSE, TRUE, 
                                        ctx, pool));

  /* Pass null for the path, to ensure error if trying to get a
     revision based on the working copy. */
  SVN_ERR (svn_client__get_revision_number
           (&src_revnum, ra_lib, sess, src_revision, NULL, pool));

  SVN_ERR (ra_lib->get_latest_revnum (sess, &youngest, pool));

  /* Use YOUNGEST for copyfrom args if not provided. */
  if (! SVN_IS_VALID_REVNUM (src_revnum))
    src_revnum = youngest;
  
  /* Verify that SRC_URL exists in the repository. */
  SVN_ERR (ra_lib->check_path (&src_kind, sess, src_rel, src_revnum, pool));
  if (src_kind == svn_node_none)
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FOUND, NULL,
       "path `%s' does not exist in revision `%" SVN_REVNUM_T_FMT "'",
       src_url, src_revnum);

  /* Figure out the basename that will result from this operation. */
  SVN_ERR (ra_lib->check_path (&dst_kind, sess, dst_rel, youngest, pool));
  if ((dst_kind == svn_node_none)
      || (dst_kind == svn_node_file))
    {
      svn_path_split (dst_url, &unused, &base_name, pool);
      if (dst_pieces)
        apr_array_pop (dst_pieces);
    }
  else if (dst_kind == svn_node_dir)
    {
      /* As a matter of client-side policy, we prevent overwriting any
         pre-existing directory.  So we temporarily append src_url's
         basename to dst_rel, and see if that already exists.  */
      svn_node_kind_t some_kind;
      const char *hypothetical_repos_path;
      
      base_name = svn_path_basename (src_url, pool);
      hypothetical_repos_path 
        = svn_path_join (dst_rel ? dst_rel : "", 
                         svn_path_uri_decode (base_name, pool), pool);
      SVN_ERR (ra_lib->check_path (&some_kind, sess,
                                   hypothetical_repos_path, youngest, pool));
      if (some_kind != svn_node_none)
        return svn_error_createf (SVN_ERR_FS_ALREADY_EXISTS, NULL,
                                  "fs path `%s' already exists.",
                                  hypothetical_repos_path);
    }
  else
    {
      return svn_error_createf (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                                "unrecognized node kind of '%s'.", dst_url);
    }

  /* Fetch RA commit editor. */
  SVN_ERR (ra_lib->get_commit_editor (sess, &editor, &edit_baton,
                                      &committed_rev,
                                      &committed_date,
                                      &committed_author,
                                      message, pool));

  /* ### Avoiding iteration pools in the loops below until we know
     they're necessary.  If there are performance problems with this
     function, that's the first place to look. */

  /* Drive that editor, baby! */
  SVN_ERR (editor->open_root (edit_baton, youngest, pool, &root_baton));

  /* Stuff the root baton here for convenience. */
  batons[i] = root_baton;

  /* Open directories down to the place where we need to make our
     copy. */
  telepath = "";
  if (dst_pieces && dst_pieces->nelts)
    {
      /* open_directory() all the way down to DST's parent. */
      while (i < dst_pieces->nelts)
        {
          piece = (((const char **)(dst_pieces)->elts)[i]);
          telepath = svn_path_join (telepath, piece, pool);
          SVN_ERR (editor->open_directory (telepath, batons[i], 
                                           youngest, pool, &(batons[i + 1])));
          i++;
        }
    }
  /* Add our file/dir with copyfrom history. */
  telepath = svn_path_join (telepath, 
                            svn_path_uri_decode (base_name, pool), 
                            pool);
  if (src_kind == svn_node_dir)
    {
      SVN_ERR (editor->add_directory (telepath, batons[i], src_url,
                                      src_revnum, pool, &baton));
      SVN_ERR (editor->close_directory (baton, pool));
    }
  else
    {
      SVN_ERR (editor->add_file (telepath, batons[i], src_url,
                                 src_revnum, pool, &baton));
      SVN_ERR (editor->close_file (baton, NULL, pool));
    }

  /* Now, close up all those batons (except the root
     baton). */
  while (i)
    {
      SVN_ERR (editor->close_directory (batons[i], pool));
      batons[i--] = NULL;
    }

  /* If this was a move, we need to remove the SRC_URL. */
  telepath = "";
  if (is_move)
    {
      /* If SRC_PIECES is NULL, we're trying to move a directory into
         itself (or one of its chidren...we should have caught that by
         now). */
      assert (src_pieces != NULL);

      /* open_directory() all the way down to SRC's parent. */
      while (i < (src_pieces->nelts - 1))
        {
          piece = (((const char **)(src_pieces)->elts)[i]);
          telepath = svn_path_join (telepath, piece, pool);
          SVN_ERR (editor->open_directory (telepath, batons[i], 
                                           youngest, pool, &(batons[i + 1])));
          i++;
        }
          
      /* Delete SRC. */
      piece = (((const char **)(src_pieces)->elts)[i]);
      telepath = svn_path_join (telepath, piece, pool);
      SVN_ERR (editor->delete_entry (telepath, SVN_INVALID_REVNUM, 
                                     batons[i], pool));

      /* Now, close up all those batons (except the root
         baton). */
      while (i)
        {
          SVN_ERR (editor->close_directory (batons[i--], pool));
        }
    }

  /* Turn off the lights, close up the shop, and go home. */
  SVN_ERR (editor->close_directory (batons[0], pool));
  SVN_ERR (editor->close_edit (edit_baton, pool));

  /* Fill in the commit_info structure. */
  *commit_info = svn_client__make_commit_info (committed_rev,
                                               committed_author,
                                               committed_date,
                                               pool);

  return SVN_NO_ERROR;
}



static svn_error_t *
remove_tmpfiles (apr_hash_t *tempfiles,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  /* Split if there's nothing to be done. */
  if (! tempfiles)
    return SVN_NO_ERROR;

  /* Clean up any tempfiles. */
  for (hi = apr_hash_first (pool, tempfiles); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      svn_node_kind_t kind;

      if (cancel_func)
        SVN_ERR (cancel_func (cancel_baton));

      apr_hash_this (hi, &key, &keylen, &val);
      SVN_ERR (svn_io_check_path ((const char *)key, &kind, pool));
      if (kind == svn_node_file)
        SVN_ERR (svn_io_remove_file ((const char *)key, pool));
    }

  return SVN_NO_ERROR;
}



static svn_error_t *
reconcile_errors (svn_error_t *commit_err,
                  svn_error_t *unlock_err,
                  svn_error_t *cleanup_err,
                  apr_pool_t *pool)
{
  svn_error_t *err;

  /* Early release (for good behavior). */
  if (! (commit_err || unlock_err || cleanup_err))
    return SVN_NO_ERROR;

  /* If there was a commit error, start off our error chain with
     that. */
  if (commit_err)
    {
      commit_err = svn_error_quick_wrap 
        (commit_err, "Commit failed (details follow):");
      err = commit_err;
    }

  /* Else, create a new "general" error that will lead off the errors
     that follow. */
  else
    err = svn_error_create (SVN_ERR_BASE, NULL,
                            "Commit succeeded, but other errors follow:");

  /* If there was an unlock error... */
  if (unlock_err)
    {
      /* Wrap the error with some headers. */
      unlock_err = svn_error_quick_wrap 
        (unlock_err, "Error unlocking locked dirs (details follow):");

      /* Append this error to the chain. */
      svn_error_compose (err, unlock_err);
    }

  /* If there was a cleanup error... */
  if (cleanup_err)
    {
      /* Wrap the error with some headers. */
      cleanup_err = svn_error_quick_wrap 
        (cleanup_err, "Error in post-commit clean-up (details follow):");

      /* Append this error to the chain. */
      svn_error_compose (err, cleanup_err);
    }

  return err;
}



static svn_error_t *
wc_to_repos_copy (svn_client_commit_info_t **commit_info,
                  const char *src_path, 
                  const char *dst_url, 
                  svn_client_ctx_t *ctx,
                  const char *message,
                  apr_pool_t *pool)
{
  const char *anchor, *target, *parent, *base_name;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  svn_node_kind_t src_kind, dst_kind;
  svn_revnum_t committed_rev = SVN_INVALID_REVNUM;
  const char *committed_date = NULL;
  const char *committed_author = NULL;
  apr_hash_t *committables, *tempfiles = NULL;
  svn_wc_adm_access_t *adm_access, *dir_access;
  apr_array_header_t *commit_items;
  svn_error_t *cmt_err = SVN_NO_ERROR;
  svn_error_t *unlock_err = SVN_NO_ERROR;
  svn_error_t *cleanup_err = SVN_NO_ERROR;
  svn_boolean_t commit_in_progress = FALSE;
  const char *base_path;
  const char *base_url;
  const char *auth_dir;

  /* The commit process uses absolute paths, so we need to open the access
     baton using absolute paths, and so we really need to use absolute
     paths everywhere. */
  SVN_ERR (svn_path_get_absolute (&base_path, src_path, pool));

  svn_path_split (base_path, &parent, &base_name, pool);
  SVN_ERR (svn_wc_adm_open (&adm_access, NULL, parent, FALSE, TRUE, pool));

  /* Split the DST_URL into an anchor and target. */
  svn_path_split (dst_url, &anchor, &target, pool);

  /* Get the RA vtable that matches URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, anchor, pool));

  /* Open an RA session for the anchor URL. */
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, anchor, parent,
                                        adm_access, NULL, TRUE, TRUE, 
                                        ctx, pool));

  /* Figure out the basename that will result from this operation. */
  SVN_ERR (ra_lib->check_path (&dst_kind, session, 
                               svn_path_uri_decode (target, pool),
                               SVN_INVALID_REVNUM, pool));
  
  /* BASE_URL defaults to DST_URL. */
  base_url = apr_pstrdup (pool, dst_url);
  if (dst_kind == svn_node_none)
    {
      /* DST_URL doesn't exist under it's parent URL, so the URL we
         will be creating is DST_URL. */
    }
  else if (dst_kind == svn_node_dir)
    {
      /* DST_URL is an existing directory URL.  The URL we will be
         creating, then, is DST_URL+BASENAME. */
      base_url = svn_path_url_add_component (base_url, base_name, pool);
    }
  else
    {
      /* DST_URL is an existing file, which can't be overwritten or
         used as a container, so error out. */
      return svn_error_createf (SVN_ERR_FS_ALREADY_EXISTS, NULL,
                                "file `%s' already exists.", dst_url);
    }

  /* Crawl the working copy for commit items. */
  SVN_ERR (svn_io_check_path (base_path, &src_kind, pool));
  if (src_kind == svn_node_dir)
     SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access, base_path, pool));
  else
    dir_access = adm_access;
  if ((cmt_err = svn_client__get_copy_committables (&committables, 
                                                    base_url,
                                                    base_path,
                                                    dir_access,
                                                    ctx,
                                                    pool)))
    goto cleanup;

  /* ### todo: There should be only one hash entry, which currently
     has a hacked name until we have the entries files storing
     canonical repository URLs.  Then, the hacked name can go away and
     be replaced with a entry->repos (or whereever the entry's
     canonical repos URL is stored). */
  if (! ((commit_items = apr_hash_get (committables, 
                                       SVN_CLIENT__SINGLE_REPOS_NAME, 
                                       APR_HASH_KEY_STRING))))
    goto cleanup;

  /* Sort and condense our COMMIT_ITEMS. */
  if ((cmt_err = svn_client__condense_commit_items (&base_url, 
                                                    commit_items, 
                                                    pool)))
    goto cleanup;

  /* Open an RA session to BASE_URL. */
  SVN_ERR (svn_client__default_auth_dir (&auth_dir, base_path, pool));
  if ((cmt_err = svn_client__open_ra_session (&session, ra_lib, base_url,
                                              auth_dir, NULL, commit_items,
                                              FALSE, FALSE,
                                              ctx, pool)))
    goto cleanup;

  /* Fetch RA commit editor. */
  if ((cmt_err = ra_lib->get_commit_editor (session, &editor, &edit_baton, 
                                            &committed_rev, &committed_date, 
                                            &committed_author, message, pool)))
    goto cleanup;

  /* Make a note that we have a commit-in-progress. */
  commit_in_progress = TRUE;

  /* Perform the commit. */
  cmt_err = svn_client__do_commit (base_url, commit_items, adm_access,
                                   editor, edit_baton, 
                                   0, /* ### any notify_path_offset needed? */
                                   &tempfiles, ctx, pool);

  commit_in_progress = FALSE;

  /* Sleep to ensure timestamp integrity. */
  svn_sleep_for_timestamps ();

 cleanup:
  /* Abort the commit if it is still in progress. */
  if (commit_in_progress)
    editor->abort_edit (edit_baton, pool); /* ignore return value */

  /* ### Under what conditions should we remove the locks? */
  unlock_err = svn_wc_adm_close (adm_access);

  /* Remove any outstanding temporary text-base files. */
  if (tempfiles)
    cleanup_err = remove_tmpfiles (tempfiles,
                                   ctx->cancel_func, ctx->cancel_baton,
                                   pool);

  /* Fill in the commit_info structure */
  *commit_info = svn_client__make_commit_info (committed_rev, 
                                               committed_author, 
                                               committed_date, pool);

  return reconcile_errors (cmt_err, unlock_err, cleanup_err, pool);
}


/*
   ### 838 OPTIONAL_ADM_ACCESS is an access baton with a write lock for the
   ### parent of DST_PATH. This parameter should be removed when issue 838
   ### stops using svn_client_copy.
*/
static svn_error_t *
repos_to_wc_copy (const char *src_url,
                  const svn_opt_revision_t *src_revision,
                  const char *dst_path, 
                  svn_wc_adm_access_t *optional_adm_access,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  void *ra_baton, *sess;
  svn_ra_plugin_t *ra_lib;
  svn_node_kind_t src_kind, dst_kind;
  svn_revnum_t src_revnum;
  svn_wc_adm_access_t *adm_access;
  apr_hash_t *props = NULL;
  apr_hash_index_t *hi;
  const char *src_uuid = NULL, *dst_uuid = NULL;
  svn_boolean_t same_repositories;
  const char *auth_dir;
  svn_opt_revision_t *revision = apr_pcalloc (pool, sizeof(*revision));

  /* Get the RA vtable that matches URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, src_url, pool));

  SVN_ERR (svn_client__default_auth_dir (&auth_dir, dst_path, pool));

  /* Open a repository session to the given URL. We do not (yet) have a
     working copy, so we don't have a corresponding path and tempfiles
     cannot go into the admin area. We do want to store the resulting
     auth data, though, once the WC is built. */
  SVN_ERR (svn_client__open_ra_session (&sess, ra_lib, src_url, auth_dir,
                                        NULL, NULL, FALSE, TRUE, 
                                        ctx, pool));
      
  /* Pass null for the path, to ensure error if trying to get a
     revision based on the working copy.  And additionally, we can't
     pass an 'unspecified' revnum to the update reporter;  assume HEAD
     if not specified. */
  revision->kind = src_revision->kind;
  revision->value = src_revision->value;
  if (revision->kind == svn_opt_revision_unspecified)
    revision->kind = svn_opt_revision_head;

  SVN_ERR (svn_client__get_revision_number
           (&src_revnum, ra_lib, sess, revision, NULL, pool));

  /* Verify that SRC_URL exists in the repository. */
  SVN_ERR (ra_lib->check_path (&src_kind, sess, "", src_revnum, pool));
  if (src_kind == svn_node_none)
    {
      if (SVN_IS_VALID_REVNUM (src_revnum))
        return svn_error_createf
          (SVN_ERR_FS_NOT_FOUND, NULL,
           "path `%s' not found in revision `%" SVN_REVNUM_T_FMT "'",
           src_url, src_revnum);
      else
        return svn_error_createf
          (SVN_ERR_FS_NOT_FOUND, NULL,
           "path `%s' not found in head revision", src_url);
    }

  /* There are two interfering sets of cases to watch out for here:
   *
   * First set:
   *
   *   1) If DST_PATH does not exist, then great.  We're going to
   *      create a new entry in its parent.
   *   2) If it does exist, then it must be a directory and we're
   *      copying to a new entry inside that dir (the entry's name is
   *      the basename of SRC_URL).
   *
   * But while that's all going on, we must also remember:
   *
   *   A) If SRC_URL is a directory in the repository, we can check
   *      it out directly, no problem.
   *   B) If SRC_URL is a file, we have to manually get the editor
   *      started, since there won't be a root to open.
   *
   * I'm going to ignore B for the moment, and implement cases 1 and
   * 2 under A.
   */

  /* First, figure out about dst. */
  SVN_ERR (svn_io_check_path (dst_path, &dst_kind, pool));
  if (dst_kind == svn_node_dir)
    {
      const char *base_name;
      svn_path_split (src_url, NULL, &base_name, pool);
      dst_path = svn_path_join (dst_path, 
                                svn_path_uri_decode (base_name, pool),
                                pool);
    }
  else if (dst_kind != svn_node_none)  /* must be a file */
    {
      return svn_error_createf (SVN_ERR_ENTRY_EXISTS, NULL,
                                "file `%s' already exists.", dst_path);
    }

  /* Now that dst_path has possibly been reset, check that there's
     nothing in the way of the upcoming checkout. */
  SVN_ERR (svn_io_check_path (dst_path, &dst_kind, pool));
  if (dst_kind != svn_node_none)
    return svn_error_createf (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                              "`%s' is in the way", dst_path);

  if (! optional_adm_access)
    SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, dst_path, TRUE, FALSE,
                                    pool));
  else
    adm_access = optional_adm_access;

  /* Decide whether the two repositories are the same or not. */
  { 
    svn_error_t *src_err, *dst_err;
    const char *parent;
   
    /* Get the repository uuid of SRC_URL */
    src_err = ra_lib->get_uuid (sess, &src_uuid, pool);
    if (src_err && src_err->apr_err != SVN_ERR_RA_NO_REPOS_UUID)
      return src_err;

    /* Get repository uuid of dst's parent directory, since dst may
       not exist.  ### TODO:  we should probably walk up the wc here,
       in case the parent dir has an imaginary URL.  */
    svn_path_split (dst_path, &parent, NULL, pool);
    dst_err = svn_client_uuid_from_path (&dst_uuid, parent, adm_access,
                                         ctx, pool);
    if (dst_err && dst_err->apr_err != SVN_ERR_RA_NO_REPOS_UUID)
      return dst_err;
    
    /* If either of the UUIDs are nonexistent, then at least one of
       the repositories must be very old.  Rather than punish the
       user, just assume the repositories are different, so no
       copy-history is attempted. */
    if (src_err || dst_err || (! src_uuid) || (! dst_uuid))
      same_repositories = FALSE;
        
    else
      same_repositories = (strcmp (src_uuid, dst_uuid) == 0) ? TRUE : FALSE; 
  }

  if (src_kind == svn_node_dir)
    {    
      const svn_delta_editor_t *editor;
      void *edit_baton;
      const svn_ra_reporter_t *reporter;
      void *report_baton;

      /* Get a checkout editor and wrap it. */
      SVN_ERR (svn_wc_get_checkout_editor (dst_path, src_url, src_revnum, 1,
                                           ctx->notify_func, ctx->notify_baton,
                                           ctx->cancel_func, ctx->cancel_baton,
                                           &editor, &edit_baton,
                                           NULL, pool));
      
      /* Check out the new tree.  The parent dir will get no entry, so
         it will be as if the new tree isn't really there yet. */
      SVN_ERR (ra_lib->do_update (sess,
                                  &reporter, &report_baton,
                                  src_revnum,
                                  NULL, /* no sub-target */
                                  TRUE, /* recurse */
                                  editor, edit_baton, pool));

      SVN_ERR (reporter->set_path (report_baton, "", src_revnum,
                                   TRUE, /* "help, my dir is empty!" */
                                   pool));

      SVN_ERR (reporter->finish_report (report_baton));               

      if ((! SVN_IS_VALID_REVNUM (src_revnum))
          && same_repositories)
        {
          /* If we just checked out from the "head" revision, that's fine,
             but we don't want to pass a '-1' as a copyfrom_rev to
             svn_wc_add().  That function will dump it right into the
             entry, and when we try to commit later on, the
             'add-dir-with-history' step will be -very- unhappy; it only
             accepts specific revisions.
             
             On the other hand, we *could* say that -1 is a legitimate
             copyfrom_rev, but I think that's bogus.  Somebody made a copy
             from a particular revision;  if they wait a long time to
             commit, it would be terrible if the copied happened from a
             newer revision!! */
          
          /* We just did a checkout; whatever revision we just got, that
             should be the copyfrom_revision when we commit later. */
          const svn_wc_entry_t *d_entry;
          svn_wc_adm_access_t *dst_access;
          SVN_ERR (svn_wc_adm_open (&dst_access, adm_access, dst_path,
                                    TRUE, TRUE, pool));
          SVN_ERR (svn_wc_entry (&d_entry, dst_path, dst_access, FALSE, pool));
          src_revnum = d_entry->revision;
        }

    } /* end directory case */

  else if (src_kind == svn_node_file)
    {
      apr_status_t status;
      svn_stream_t *fstream;
      apr_file_t *fp;
      svn_revnum_t fetched_rev = 0;
      
      /* Open DST_PATH for writing. */
      SVN_ERR_W (svn_io_file_open (&fp, dst_path,
                                   (APR_CREATE | APR_WRITE),
                                   APR_OS_DEFAULT, pool),
                 "failed to open file for writing.");

      /* Create a generic stream that operates on this file.  */
      fstream = svn_stream_from_aprfile (fp, pool);
      
      /* Have the RA layer 'push' data at this stream.  We pass a
         relative path of "", because we opened SRC_URL, which is
         already the full URL to the file. */         
      SVN_ERR (ra_lib->get_file (sess, "", src_revnum, fstream, 
                                 &fetched_rev, &props, pool));

      /* Close the file. */
      status = apr_file_close (fp);
      if (status)
        return svn_error_createf (status, NULL,
                                  "failed to close file '%s'.",
                                  dst_path);   
     
      /* Also, if SRC_REVNUM is invalid ('head'), then FETCHED_REV is now
         equal to the revision that was actually retrieved.  This is
         the value we want to use as 'copyfrom_rev' in the call to
         svn_wc_add() below. */
      if (! SVN_IS_VALID_REVNUM (src_revnum))
        src_revnum = fetched_rev;
    }
  
  /* Schedule the new item for addition, or addition-with-history.
     
     If the new item is a directory, the URLs will be recursively
     rewritten, wcprops removed, and everything marked as 'copied',
     assuming that the src and dst are from the same repository.
     See comment in svn_wc_add()'s doc about whether svn_wc_add is the
     appropriate place for this. */
  if (same_repositories)
    {
      /* Schedule dst_path for addition in parent, with copy history.
         (This function also recursively puts a 'copied' flag on every
         entry) */
      SVN_ERR (svn_wc_add (dst_path, adm_access, src_url, src_revnum,
                           ctx->cancel_func, ctx->cancel_baton, 
                           ctx->notify_func, ctx->notify_baton, pool));
    }
  else
    {
      if (src_kind == svn_node_dir)
        {
          /* ### Someday, we would just call svn_wc_add(), as above,
             but with no copyfrom args.  I.e. in the
             directory-foreign-UUID case, we still want everything
             scheduled for addition, URLs rewritten, and wcprop cache
             deleted, but WITHOUT any copied flags or copyfrom urls.
             Unfortunately, svn_wc_add() is such a mess that it chokes
             at the moment when we pass a NULL copyfromurl. */

          return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                   "Source URL is from foreign repository.");
        }
      
      /* Recursively schedule unversioned tree for addition, sans history. */
      SVN_ERR (svn_client__add (dst_path, TRUE /* recursive */,
                                adm_access, ctx, pool));
    }

  /* If any properties were fetched (in the file case), apply those
     changes now. */
  if (props)
    {
      for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi)) 
        {
          const void *key;
          void *val;
          enum svn_prop_kind kind;
          
          apr_hash_this (hi, &key, NULL, &val);
          
          /* We only want to set 'normal' props.  For now, we're
             ignoring any wc props (they're not needed when we commit
             an addition), and we're ignoring entry props (they're
             written to the entries file as part of the post-commit
             processing).  */
          kind = svn_property_kind (NULL, key);
          if (kind == svn_prop_regular_kind)
            SVN_ERR (svn_wc_prop_set (key, val, dst_path, adm_access, pool));
        }
    }

  if (! optional_adm_access)
    SVN_ERR (svn_wc_adm_close (adm_access));

  return SVN_NO_ERROR;
}

/*
   ### 838 OPTIONAL_ADM_ACCESS is an access baton with a write lock for the
   ### parent of DST_PATH. This parameter should be removed when issue 838
   ### stops using svn_client_copy.
*/
static svn_error_t *
setup_copy (svn_client_commit_info_t **commit_info,
            const char *src_path,
            const svn_opt_revision_t *src_revision,
            const char *dst_path,
            svn_wc_adm_access_t *optional_adm_access,
            svn_boolean_t is_move,
            svn_boolean_t force,
            svn_client_ctx_t *ctx,
            apr_pool_t *pool)
{
  svn_boolean_t src_is_url, dst_is_url;
  const char *message;

  /* Are either of our paths URLs? */
  src_is_url = svn_path_is_url (src_path);
  dst_is_url = svn_path_is_url (dst_path);

  if (svn_path_is_child (src_path, dst_path, pool))
    return svn_error_createf
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       "cannot copy path '%s' into its own child '%s'",
       src_path, dst_path);

  if (is_move)
    {
      if (src_is_url == dst_is_url)
        {
          if (strcmp (src_path, dst_path) == 0)
            return svn_error_createf
              (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
               "cannot move path '%s' into itself",
               src_path);
        }
      else
        {
          /* Disallow moves between the working copy and the repository. */
          return svn_error_create 
            (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
             "no support for repos <--> working copy moves");
        }

      /* It doesn't make sense to specify revisions in a move. */

      /* ### todo: this check could fail wrongly.  For example,
         someone could pass in an svn_opt_revision_number that just
         happens to be the HEAD.  It's fair enough to punt then, IMHO,
         and just demand that the user not specify a revision at all;
         beats mucking up this function with RA calls and such. */ 
      if (src_revision->kind != svn_opt_revision_unspecified
          && src_revision->kind != svn_opt_revision_head)
        {
          return svn_error_create
            (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
             "cannot specify revisions with move operations");
        }
    }
  else
    {
      if (!src_is_url)
        {
          if (src_revision->kind != svn_opt_revision_unspecified
              && src_revision->kind != svn_opt_revision_head)
            {
              /* We can convert the working copy path to a URL based on the
                 entries file. */
              svn_wc_adm_access_t *adm_access;  /* ### FIXME local */
              const svn_wc_entry_t *entry;
              SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, src_path,
                                              FALSE, FALSE, pool));
              SVN_ERR (svn_wc_entry (&entry, src_path, adm_access, FALSE,
                                     pool));
              SVN_ERR (svn_wc_adm_close (adm_access));
              src_path = entry->url;
              src_is_url = TRUE;
            }
        }
    }

  /* Create a new commit item and add it to the array. */
  if (dst_is_url && ctx->log_msg_func)
    {
      svn_client_commit_item_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items 
        = apr_array_make (pool, 1, sizeof (item));
      
      item = apr_pcalloc (pool, sizeof (*item));
      item->url = apr_pstrdup (pool, dst_path);
      item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
      (*((svn_client_commit_item_t **) apr_array_push (commit_items))) 
        = item;
      
      SVN_ERR ((*ctx->log_msg_func) (&message, &tmp_file, commit_items, 
                                     ctx->log_msg_baton, pool));
      if (! message)
        return SVN_NO_ERROR;
    }
  else
    message = "";

  /* Now, call the right handler for the operation. */
  if ((! src_is_url) && (! dst_is_url))
    SVN_ERR (wc_to_wc_copy (src_path, dst_path, optional_adm_access,
                            is_move, force,
                            ctx,
                            pool));

  else if ((! src_is_url) && (dst_is_url))
    {
      SVN_ERR (wc_to_repos_copy (commit_info, src_path, dst_path, 
                                 ctx, message, 
                                 pool));
    }
  else if ((src_is_url) && (! dst_is_url))
    {
      SVN_ERR (repos_to_wc_copy (src_path, src_revision, 
                                 dst_path, optional_adm_access, ctx,
                                 pool));
    }
  else
    {
      SVN_ERR (repos_to_repos_copy (commit_info, src_path, src_revision,
                                    dst_path, ctx, message, is_move,
                                    pool));
    }

  return SVN_NO_ERROR;
}



/* Public Interfaces */

/*
   ### 838 OPTIONAL_ADM_ACCESS is an access baton with a write lock for the
   ### parent of DST_PATH. This parameter should be removed when issue 838
   ### stops using svn_client_copy.
*/
svn_error_t *
svn_client_copy (svn_client_commit_info_t **commit_info,
                 const char *src_path,
                 const svn_opt_revision_t *src_revision,
                 const char *dst_path,
                 svn_wc_adm_access_t *optional_adm_access,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return setup_copy (commit_info, 
                     src_path, src_revision, dst_path, optional_adm_access,
                     FALSE /* is_move */,
                     TRUE /* force, set to avoid deletion check */,
                     ctx,
                     pool);
}


svn_error_t *
svn_client_move (svn_client_commit_info_t **commit_info,
                 const char *src_path,
                 const svn_opt_revision_t *src_revision,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return setup_copy (commit_info,
                     src_path, src_revision, dst_path, NULL,
                     TRUE /* is_move */,
                     force,
                     ctx,
                     pool);
}
