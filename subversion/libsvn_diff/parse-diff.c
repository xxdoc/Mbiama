  svn_patch_t *patch;
 * is being read.
      if (eol)
        *eol = NULL;
                                   result_pool, scratch_pool));
      SVN_ERR(svn_io_file_seek(file, APR_CUR, &range->current, scratch_pool));
      /* Return the line as-is. */
      if (eol)
        *eol = NULL;
            continue; /* Proceed to the next line in the patch. */
                *prop_operation = svn_diff_op_added;
                *prop_operation = svn_diff_op_deleted;
   state_start,           /* initial */
   state_git_diff_seen,   /* diff --git */
   state_git_tree_seen,   /* a tree operation, rather then content change */
   state_git_minus_seen,  /* --- /dev/null; or --- a/ */
   state_git_plus_seen,   /* +++ /dev/null; or +++ a/ */
   state_move_from_seen,  /* rename from foo.c */
   state_copy_from_seen,  /* copy from foo.c */
   state_minus_seen,      /* --- foo.c */
   state_unidiff_found,   /* valid start of a regular unidiff header */
   state_git_header_found /* valid start of a --git diff header */
  {"--- ",          state_start,            diff_minus},
  {"+++ ",          state_minus_seen,       diff_plus},
  {"diff --git",    state_start,            git_start},
  {"--- a/",        state_git_diff_seen,    git_minus},
  {"--- a/",        state_git_tree_seen,    git_minus},
  {"--- /dev/null", state_git_tree_seen,    git_minus},
  {"+++ b/",        state_git_minus_seen,   git_plus},
  {"+++ /dev/null", state_git_minus_seen,   git_plus},
  {"rename from ",  state_git_diff_seen,    git_move_from},
  {"rename to ",    state_move_from_seen,   git_move_to},
  {"copy from ",    state_git_diff_seen,    git_copy_from},
  {"copy to ",      state_copy_from_seen,   git_copy_to},
  {"new file ",     state_git_diff_seen,    git_new_file},
  {"deleted file ", state_git_diff_seen,    git_deleted_file},
      if (state == state_unidiff_found || state == state_git_header_found)
      else if (state == state_git_tree_seen && line_after_tree_header_read)
          /* git patches can contain an index line after the file mode line */
          if (!starts_with(line->data, "index "))
          {
            /* We have a valid diff header for a patch with only tree changes.
             * Rewind to the start of the line just read, so subsequent calls
             * to this function don't end up skipping the line -- it may
             * contain a patch. */
            SVN_ERR(svn_io_file_seek(patch_file->apr_file, APR_SET, &last_line,
                    scratch_pool));
            break;
          }
      else if (state == state_git_tree_seen)
               && state != state_git_diff_seen
               && !starts_with(line->data, "index "))
    SVN_ERR(parse_hunks(patch, patch_file->apr_file, ignore_whitespace,
                        result_pool, iterpool));
  if (patch)