/*
 * locking_commands.c:  Implementation of lock and unlock.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include "svn_client.h"
#include "client.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_pools.h"

#include "svn_private_config.h"


/*** Code. ***/

#if 0
/* ### This will probably not be necessary. */
/* Check if TARGET is out-of-date, meaning that it was committed to in a
   revision newer than REV. RA_LIB, SESSION is an open RA session pointing to
   TARGET's parent. */
static svn_error_t *check_out_of_date (svn_ra_plugin_t *ra_lib, void *session,
                                       svn_wc_entry_t *entry,
                                       const char *path,
                                       apr_pool_t *pool)
{
  apr_hash_t *dirents;
  svn_dirent_t *dirent;

  /* Get a directory listing of the target's parent. */
  SVN_ERR (ra_lib->get_dir (session, "", SVN_INVALID_REVNUM, &dirents, NULL,
                            NULL, pool));
  dirent = apr_hash_get(dirents, entry->name, APR_HASH_KEY_STRING);
  
  /* Schedule add is handled specially. */
  if (entry->schedule == svn_wc_schedule_add)
    {
      /* If the path exists, it is out-of-date in the WC. */
      if (dirent)
        return svn_error_createf (SVN_ERR_RA_OUT_OF_DATE, NULL,
                                  _("'%s' already exists in the repository"),
                                  svn_path_local_style (path, pool));
      else
        return SVN_NO_ERROR;
    }

  if (! dirent)
    return svn_error_createf (SVN_ERR_RA_OUT_OF_DATE, NULL,
                              _("'%s' does no longer exist in the repository"),
                              svn_path_local_style (path, pool));

  /* Path exists, check that our revision is the latest. */
  if (entry->revision < dirent->created_rev)
    return svn_error_createf (SVN_ERR_RA_OUT_OF_DATE, NULL,
                              _("The repository has a newer revision of '%s' "
                                "than the working copy"),
                              svn_path_local_style (path, pool));

  return SVN_NO_ERROR;
}
#endif

/* For use with store_locks_callback, below. */
struct lock_baton
{
  svn_lock_callback_t nested_callback;
  void *nested_baton;
  svn_wc_adm_access_t *adm_access;
  apr_pool_t *pool;
};


/* This callback is called by the ra_layer for each path locked.
 * BATON is a 'struct lock_baton *', PATH is the path being locked,
 * and LOCK is the lock itself.
 *
 * If BATON->adm_access is not null, then this function either stores
 * the LOCK on PATH or removes any lock tokens from PATH (depending on
 * whether DO_LOCK is true or false respectively), but only if RA_ERR
 * is null, or (in the unlock case) is something other than
 * SVN_ERR_FS_LOCK_OWNER_MISMATCH.
 *
 * Assuming the above went without error, invoke BATON->nested_callback
 * with BATON->nested_baton, PATH, DO_LOCK, LOCK, and RA_ERR as
 * arguments.
 */
static svn_error_t *
store_locks_callback (void *baton, 
                      const char *path, 
                      svn_boolean_t do_lock,
                      const svn_lock_t *lock,
                      svn_error_t *ra_err)
{
  struct lock_baton *lb = baton;
  svn_wc_adm_access_t *adm_access;
  const char *abs_path;

  if (lb->adm_access)
    {
      abs_path = svn_path_join (svn_wc_adm_access_path (lb->adm_access), 
                                path, lb->pool);

      SVN_ERR (svn_wc_adm_probe_retrieve (&adm_access, lb->adm_access,
                                          abs_path, lb->pool));

      if (do_lock)
        {
          if (ra_err)
            SVN_ERR (svn_wc_add_lock (abs_path, lock, adm_access, lb->pool));
        }
      else /* unlocking */
        {
          /* Remove our wc lock token either a) if we got no error, or b) if
             we got any error except for owner mismatch.  Note that the only
             errors that are handed to this callback will be locking-related
             errors. */
          if (!ra_err ||
              (ra_err && (ra_err->apr_err != SVN_ERR_FS_LOCK_OWNER_MISMATCH)))
            SVN_ERR (svn_wc_remove_lock (abs_path, adm_access, lb->pool));
        }
    }
  
  /* Call our callback, if we've got one. */
  if (lb->nested_callback)
    SVN_ERR (lb->nested_callback (lb->nested_baton, path, do_lock,
                                  lock, ra_err));

  return SVN_NO_ERROR;
}


/* Set *COMMON_PARENT to the nearest common parent of all TARGETS.
 *
 * If all the targets are local paths within the same wc, i.e.,
 * they share a common parent at some level, set *PARENT_ENTRY_P
 * and *PARENT_ADM_ACCESS_P to the entry and adm_access of that
 * common parent.  *PARENT_ADM_ACCESS_P will be associated with
 * adm_access objects for all the other paths, which are locked in the
 * working copy while we lock them in the repository.
 *
 * If all the targets are URLs in the same repository, i.e. sharing a
 * common parent URL prefix, then set *PARENT_ENTRY_P and
 * *PARENT_ADM_ACCESS_P to null.
 *
 * If there is no common parent, either because the targets are a
 * mixture of URLs and local paths, or because they simply do not
 * share a common parent, then return SVN_ERR_UNSUPPORTED_FEATURE.
 * (The value of *COMMON_PARENT and other return parameters is
 * undefined in this case.)
 *
 * DO_LOCK is TRUE for locking TARGETS, and FALSE for unlocking them.
 * FORCE is TRUE for breaking or stealing locks, and FALSE otherwise.
 *
 * Each key stored in *REL_TARGETS_P is a path relative to *COMMON_PARENT.
 * If *COMMON_PARENT is a local path, then: if DO_LOCK is true, the
 * value is a pointer to the corresponding base_revision (allocated in
 * POOL) for the path, else the value is the lock token (or "" if no
 * token found in the wc).
 *
 * If *COMMON_PARENT is a URL, then the values are a pointer to
 * SVN_INVALID_REVNUM (allocated in pool) if DO_LOCK, else "".
 */
static svn_error_t *
organize_lock_targets (const char **common_parent,
                       const svn_wc_entry_t **parent_entry_p,
                       svn_wc_adm_access_t **parent_adm_access_p,
                       apr_hash_t **rel_targets_p,
                       apr_array_header_t *targets,
                       svn_boolean_t do_lock,
                       svn_boolean_t force,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *pool)
{
  int i;
  apr_array_header_t *rel_targets = apr_array_make(pool, 1,
                                                   sizeof(const char *));
  apr_hash_t *rel_targets_ret = apr_hash_make(pool);

  /* Get the common parent and all relative paths */
  SVN_ERR (svn_path_condense_targets (common_parent, &rel_targets, targets, 
                                      FALSE, pool));

  /* svn_path_condense_targets leaves paths empty if TARGETS only had
     1 member, so we special case that. */
  if (apr_is_empty_array (rel_targets))
    {
      char *base_name = svn_path_basename (*common_parent, pool);
      *common_parent = svn_path_dirname (*common_parent, pool);

      APR_ARRAY_PUSH(rel_targets, char *) = base_name;
    }

  if (*common_parent == NULL || (*common_parent)[0] == '\0')
    return svn_error_create
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("No common parent found, unable to operate on disjoint arguments"));
  
  if (svn_path_is_url (*common_parent))
    {
      svn_revnum_t *invalid_revnum;
      invalid_revnum = apr_palloc (pool, sizeof (*invalid_revnum));
      *invalid_revnum = SVN_INVALID_REVNUM;

      for (i = 0; i < rel_targets->nelts; i++)
        {
          const char *target = ((const char **) (rel_targets->elts))[i];
          apr_hash_set (rel_targets_ret, apr_pstrdup (pool, target),
                        APR_HASH_KEY_STRING,
                        do_lock ? (void *) invalid_revnum : (void *) "");
        }
    }
  else  /* common parent is a local path */
    {
      /* Open the common parent. */
      SVN_ERR (svn_wc_adm_probe_open3 (parent_adm_access_p, NULL,
                                       *common_parent, 
                                       TRUE, 0, ctx->cancel_func, 
                                       ctx->cancel_baton, pool));  

      SVN_ERR (svn_wc_entry (parent_entry_p, *common_parent, 
                             *parent_adm_access_p, FALSE, pool));
      
      /* Verify all paths. */
      for (i = 0; i < rel_targets->nelts; i++)
        {
          svn_wc_adm_access_t *adm_access;
          const svn_wc_entry_t *entry;
          const char *target = ((const char **) (rel_targets->elts))[i];
          const char *abs_path;
          
          abs_path = svn_path_join
            (svn_wc_adm_access_path (*parent_adm_access_p), target, pool);
          
          SVN_ERR (svn_wc_adm_probe_try3 (&adm_access, *parent_adm_access_p,
                                          abs_path, TRUE, 0, ctx->cancel_func,
                                          ctx->cancel_baton, pool));
          
          SVN_ERR (svn_wc_entry (&entry, abs_path, adm_access, FALSE, pool));
          
          if (! entry)
            return svn_error_createf (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                      _("'%s' is not under version control"), 
                                      svn_path_local_style (target, pool));
          if (! entry->url)
            return svn_error_createf (SVN_ERR_ENTRY_MISSING_URL, NULL,
                                      _("'%s' has no URL"),
                                      svn_path_local_style (target, pool));
          
          if (do_lock) /* Lock. */
            {
              svn_revnum_t *revnum;
              revnum = apr_palloc (pool, sizeof (* revnum));
              *revnum = entry->revision;
              
              apr_hash_set (rel_targets_ret, apr_pstrdup (pool, target),
                            APR_HASH_KEY_STRING, revnum);
            }
          else /* Unlock. */
            {
              /* If not force, get the lock token from the WC entry. */
              if (! force)
                {
                  if (! entry->lock_token)
                    return svn_error_createf 
                      (SVN_ERR_CLIENT_MISSING_LOCK_TOKEN, NULL,
                       _("'%s' is not locked in this working copy"), target);
                  
                  apr_hash_set (rel_targets_ret, apr_pstrdup (pool, target),
                                APR_HASH_KEY_STRING, 
                                apr_pstrdup (pool, entry->lock_token));
                }
              else
                {
                  /* If breaking a lock, we shouldn't pass any lock token. */
                  apr_hash_set (rel_targets_ret, apr_pstrdup (pool, target),
                                APR_HASH_KEY_STRING, "");
                }
            }
        }
    }

  *rel_targets_p = rel_targets_ret;

  return SVN_NO_ERROR;
}


/* Set the value of each key in PATH_REVS to a pointer to the latest
   revision (allocated in POOL) of the repository open in RA_SESSION. */
static svn_error_t *
store_head_revision (svn_ra_session_t *ra_session,
                     apr_hash_t *path_revs,
                     apr_pool_t *pool)
{
  svn_revnum_t *latest = apr_palloc (pool, sizeof (*latest));
  apr_hash_index_t *hi;

  SVN_ERR (svn_ra_get_latest_revnum (ra_session, latest, pool));
  for (hi = apr_hash_first (pool, path_revs); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      apr_hash_this (hi, &key, &klen, NULL);
      apr_hash_set (path_revs, key, klen, latest);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_lock (apr_array_header_t **locks_p,
                 apr_array_header_t *targets,
                 const char *comment,
                 svn_boolean_t force,
                 svn_lock_callback_t lock_func,
                 void *lock_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const char *common_parent;
  const svn_wc_entry_t *entry;
  const char *url;
  svn_ra_session_t *ra_session;
  apr_array_header_t *locks;
  apr_hash_t *path_revs;
  struct lock_baton cb;
  svn_boolean_t is_url;

  /* Enforce that the comment be xml-escapable. */
  if (comment)
    {
      if (! svn_xml_is_xml_safe(comment, strlen(comment)))
        return svn_error_create
          (SVN_ERR_XML_UNESCAPABLE_DATA, NULL,
           _("Lock comment has illegal characters."));      
    }

  SVN_ERR (organize_lock_targets (&common_parent, &entry, &adm_access,
                                  &path_revs, targets, TRUE, force, ctx,
                                  pool));

  is_url = svn_path_is_url (common_parent);

  if (is_url)
    url = common_parent;
  else
    url = entry->url;

  /* Open an RA session to the common parent of TARGETS. */
  SVN_ERR (svn_client__open_ra_session
           (&ra_session, url, is_url ? NULL : common_parent,
            adm_access, NULL, FALSE, FALSE, ctx, pool));

  cb.pool = pool;
  cb.nested_callback = lock_func;
  cb.nested_baton = lock_baton;
  cb.adm_access = adm_access;

  /* ### This is a total crock. :-( */
  if (is_url)
    SVN_ERR (store_head_revision (ra_session, path_revs, pool));

  /* Lock the paths. */
  SVN_ERR (svn_ra_lock (ra_session, &locks, path_revs, comment, 
                        force, store_locks_callback, &cb, pool));

  /* Unlock the wc.
   * ### TODO: Why is this necessary here and yet apparently not in
   * ### svn_client_unlock?
   */
  if (adm_access)
    svn_wc_adm_close (adm_access);

 /* ###TODO Since we've got callbacks all over the place, is there
    still any point in providing the list of locks back to our
    caller? */
  *locks_p = locks;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_unlock (apr_array_header_t *targets,
                   svn_boolean_t force,
                   svn_lock_callback_t unlock_func,
                   void *lock_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const char *common_parent;
  const svn_wc_entry_t *entry;
  const char *url;
  svn_ra_session_t *ra_session;
  apr_hash_t *path_tokens;
  struct lock_baton cb;

  SVN_ERR (organize_lock_targets (&common_parent, &entry, &adm_access,
                                  &path_tokens, targets, FALSE, force, ctx,
                                  pool));

  if (svn_path_is_url (common_parent))
    {
      url = common_parent;
      /* Unlocking a URL is pointless with the 'force' flag anyway, so
         just set it if we're operating on URLs. */
      force = TRUE;
    }
  else
    {
      url = entry->url;
    }

  /* Open an RA session. */
  SVN_ERR (svn_client__open_ra_session
           (&ra_session, url,
            svn_path_is_url (common_parent) ? NULL : common_parent,
            adm_access, NULL, FALSE, FALSE, ctx, pool));

  cb.pool = pool;
  cb.nested_callback = unlock_func;
  cb.nested_baton = lock_baton;
  cb.adm_access = adm_access;

  /* Unlock the paths. */
  SVN_ERR (svn_ra_unlock (ra_session, path_tokens, force, 
                          store_locks_callback, &cb, pool));

  return SVN_NO_ERROR;
}

