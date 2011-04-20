// Copyright (c) 2011, David Pineau
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER AND CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cloudmig.h"

#define URI_ALL  "http://acs.amazonaws.com/groups/global/AllUsers"
#define URI_AUTH "http://acs.amazonaws.com/groups/global/AuthenticatedUsers"
#define URI_LOG  "http://acs.amazonaws.com/groups/s3/LogDelivery"

typedef enum
{
    ACCESS_NOGENERIC       = 0,
    ACCESS_PRIVATE         = 1 << 0,
    ACCESS_PUBLIC_READ     = 1 << 1,
    ACCESS_PUBLIC_WRITE    = 1 << 2,
    ACCESS_AUTH_READ       = 1 << 3,
    ACCESS_OWNER_READ      = 1 << 4,
    ACCESS_OWNER_FULL      = 1 << 5
} e_access;


/*
 * TODO FIXME
 *
 * This function currently manages 4/6 canned acls.
 * It does not manage the bucket_owner acls (read or full control)
 *
 */
static e_access
deduce_generic_access(const xmlChar* owner,
                      const xmlChar *perm, const xmlChar *id, bool id_is_uri)
{
    if (id_is_uri)
    {
        if (xmlStrcmp(id, (const xmlChar *)URI_ALL) == 0)
        {
            if (xmlStrcmp(perm, (const xmlChar *)"READ") == 0)
                return ACCESS_PUBLIC_READ;
            if (xmlStrcmp(perm, (const xmlChar *)"WRITE") == 0)
                return ACCESS_PUBLIC_WRITE;
        }
        else if (xmlStrcmp(id, (const xmlChar *)URI_AUTH) == 0)
        {
            if (xmlStrcmp(perm, (const xmlChar *)"READ") == 0)
                return ACCESS_AUTH_READ;
        }
    }
    else
    {
        if (xmlStrcmp(owner, id) == 0)
            return ACCESS_PRIVATE;
    }
    return ACCESS_NOGENERIC;
}

static const xmlChar*
get_grantee_id(xmlNodePtr grantee, bool *id_is_uri)
{
    while (grantee)
    {
        if (xmlStrcmp(grantee->name, (const xmlChar *)"URI") == 0)
        {
            if (grantee->xmlChildrenNode
                && xmlStrcmp(grantee->xmlChildrenNode->name,
                             (const xmlChar *)"text") == 0)
            {
                grantee = grantee->xmlChildrenNode;
                *id_is_uri = true;
                return grantee->content;
            }
        }
        else if(xmlStrcmp(grantee->name, (const xmlChar *)"ID") == 0)
        {
            if (grantee->xmlChildrenNode
                && xmlStrcmp(grantee->xmlChildrenNode->name,
                             (const xmlChar *)"text") == 0)
            {
                grantee = grantee->xmlChildrenNode;
                *id_is_uri = false;
                return grantee->content;
            }
        }
        grantee = grantee->next;
    }
    return NULL;
}

/*
 *
 * This function goes through the grant elements and deduces the generic right
 * for each one before updating the access.
 *
 */
static void
get_generic_accesses(xmlNodePtr list, const xmlChar *owner_id, int *access)
{
    bool            id_is_uri = false;
    xmlNodePtr      permission = NULL;
    xmlNodePtr      tmp = NULL;
    const xmlChar   *grantee_id = NULL;

    while (list)
    {
        if (xmlStrcmp(list->name, (const xmlChar*)"text") == 0)
        {
            list = list->next;
            continue ;
        }

        tmp = list->xmlChildrenNode;
        while (tmp)
        {
            if (xmlStrcmp(tmp->name, (const xmlChar *)"Grantee") == 0)
                grantee_id = get_grantee_id(tmp->xmlChildrenNode, &id_is_uri);
            else if (xmlStrcmp(tmp->name, (const xmlChar *)"Permission") == 0)
            {
                if (tmp->xmlChildrenNode
                    && xmlStrcmp(tmp->xmlChildrenNode->name,
                                 (const xmlChar *)"text") == 0)
                    permission = tmp->xmlChildrenNode;
            }
            tmp = tmp->next;
        }
        if (permission && grantee_id)
            *access |= deduce_generic_access(owner_id, permission->content,
                                             grantee_id, id_is_uri);
        else
        {
            cloudmig_log(WARN_LVL,
"[Migrating]: Could not retrieve ID and Permission from XML Grant entry\n");
        }

        permission = NULL;
        grantee_id = NULL;
        list = list->next;
    }
}

static dpl_canned_acl_t
get_canned_from_accesses(int access)
{
    dpl_canned_acl_t    acl = DPL_CANNED_ACL_UNDEF;

    if (access & ACCESS_PRIVATE)
    {
        acl = DPL_CANNED_ACL_PRIVATE;
        cloudmig_log(DEBUG_LVL,
        "[Migrating]: Deduced canned ACL : DPL_CANNED_ACL_PRIVATE\n");
    }

    if (access & ACCESS_PUBLIC_READ && access & ACCESS_PUBLIC_WRITE)
    {
        acl = DPL_CANNED_ACL_PUBLIC_READ_WRITE;
        cloudmig_log(DEBUG_LVL,
    "[Migrating]: Deduced canned ACL : DPL_CANNED_ACL_PUBLIC_READ_WRITE\n");
    }
    else if (access & ACCESS_PUBLIC_READ)
    {
        acl = DPL_CANNED_ACL_PUBLIC_READ;
        cloudmig_log(DEBUG_LVL,
        "[Migrating]: Deduced canned ACL : DPL_CANNED_ACL_PUBLIC_READ\n");
    }
    else if (access & ACCESS_AUTH_READ)
    {
        acl = DPL_CANNED_ACL_AUTHENTICATED_READ;
        cloudmig_log(DEBUG_LVL,
    "[Migrating]: Deduced canned ACL : DPL_CANNED_ACL_AUTHENTICATED_READ\n");
    }

    // Last resort if nothing could be identified, fallback to private.
    if (acl == DPL_CANNED_ACL_UNDEF)
        acl = DPL_CANNED_ACL_PRIVATE;

    return acl;
}

static xmlChar*
get_acl_owner_id(xmlNodePtr cur)
{
    cur = cur->xmlChildrenNode;
    while (cur != NULL)
    {
        if (xmlStrcmp(cur->name, (const xmlChar *)"Owner") == 0)
            break ;
        cur = cur->next;
    }
    if (cur == NULL)
        return NULL;

    cur = cur->xmlChildrenNode;
    while (cur != NULL)
    {
        if (xmlStrcmp(cur->name, (const xmlChar *)"ID") == 0)
        {
            if (xmlStrcmp(cur->xmlChildrenNode->name,
                          (const xmlChar *)"text") == 0)
            {
                cur = cur->xmlChildrenNode;
                break ;
            }
        }
        cur = cur->next;
    }
    return cur != NULL ? cur->content : NULL;
}


static dpl_canned_acl_t
parse_acl(char *buf, unsigned int size)
{
    dpl_canned_acl_t    acl = DPL_CANNED_ACL_UNDEF;
    xmlDocPtr           doc;
    xmlNodePtr          cur;
    xmlChar             *owner_id = NULL;
    int                 access = 0;

    doc = xmlParseMemory(buf, size);
    cur = xmlDocGetRootElement(doc);
    if (cur == NULL)
    {
        cloudmig_log(WARN_LVL, "[Migrating]: Could not parse XML : %s\n",
                     strerror(errno));
        goto xml_err;
    }

    if (xmlStrcmp(cur->name, (const xmlChar *)"AccessControlPolicy"))
    {
        cloudmig_log(WARN_LVL,
                "[Migrating]: The acl's xml root element is not right.\n");
        goto xml_err;
    }
    owner_id = get_acl_owner_id(cur);
    if (owner_id == NULL)
    {
        cloudmig_log(WARN_LVL, "[Migrating]: Could not find owner's ID.\n");
        goto xml_err;
    }

    cur = cur->xmlChildrenNode;
    while (cur)
    {
        if (xmlStrcmp(cur->name, (const xmlChar *)"AccessControlList") == 0)
        {
            get_generic_accesses(cur->xmlChildrenNode, owner_id, &access);
            break ;
        }
        cur = cur->next;
    }
    if (cur == NULL)
    {
        cloudmig_log(WARN_LVL,
        "[Migrating]: Could not find AccessControlList node in acl's xml.\n");
    }


xml_err:
    acl = get_canned_from_accesses(access);

    xmlFreeDoc(doc);

    return acl;
}

dpl_canned_acl_t
get_file_canned_acl(dpl_ctx_t* ctx, char *filename)
{
    char                *buffer = NULL;
    unsigned int        bufsize = 0;
    dpl_status_t        dplret = DPL_SUCCESS;
    char                *bucket;
    dpl_canned_acl_t    acl = DPL_CANNED_ACL_UNDEF;

    cloudmig_log(DEBUG_LVL,
                 "[Migrating]: getting acl of file %s...\n",
                 filename);

    bucket = filename;
    filename = index(filename, ':');
    *filename = '\0';
    ++filename;

    dplret = dpl_get(ctx, bucket, filename, "acl", NULL,
                     &buffer, &bufsize, NULL);
    if (dplret != DPL_SUCCESS)
    {
        cloudmig_log(INFO_LVL,
                     "[Migrating]: Could not retrieve acl of file %s : %s\n",
                     filename, dpl_status_str(dplret));
        goto err;
    }

    acl = parse_acl(buffer, bufsize);

err:
    --filename;
    *filename = ':';

    free(buffer);

    return acl;
}
