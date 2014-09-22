local URI = require 'RDL.uri'
local serialize = require 'RDL.serialize'

local MemStore = {}
MemStore.__index = MemStore

local function new (args)
    local d = {
        __types = {},
        __resources = {},
        __hierarchy = {},
    }
    return setmetatable (d, MemStore)
end

--
-- https://stackoverflow.com/questions/640642/how-do-you-copy-a-lua-table-by-value
--
local function deepcopy_no_metatable(o, seen)
    seen = seen or {}
    if o == nil then return nil end
    if seen[o] then return seen[o] end

    local no
    if type(o) == 'table' then
        no = {}
        seen[o] = no

        for k, v in next, o, nil do
            no[deepcopy_no_metatable(k, seen)] = deepcopy_no_metatable(v, seen)
        end
        --setmetatable(no, deepcopy(getmetatable(o), seen))
    else -- number, string, boolean, etc
        no = o
    end
    return no
end

local function deepcompare (t1, t2, seen)
    seen = seen or {}
    if seen[t1] and seen[t2] then return true end

    local type1, type2 = type(t1), type(t2)
    if type1 ~= type2 then
        return false
    end

    if type1 ~= "table" then
        return t1 == t2
    end

    -- Mark both these tables as visited already to avoid
    --  recursing back up the tree.
    --
    seen [t1] = true
    seen [t2] = true

    local checked = {}
    for k,v in pairs (t1) do
        checked [k] = true
        -- If this object has not been visited before then
        --  continue comparison (only tables are recorded in 'seen')
        --  so normal values will still be compared at each iteration.
        if not seen [v] and not deepcompare (v, t2[k], seen) then
            return false
        end
    end
    for k,v in pairs (t2) do
        -- if we didn't see key 'k' in traversal of t1, then
        --  these two tables are not equal (extra key in t2)
        if not checked[k] then
            return false
        end
    end

    return true
end

--
-- Convert tags table given as
--
--  { "tag1", "tag2", tag3 = 100 }
-- to
--  { tag1 = true, tag2 = true, tag3 = 100 }
--
local function convert_tags_table (t)
    local result = deepcopy_no_metatable (t)
    for i,v in ipairs (result) do
        result [v] = true
        result [i] = nil
    end
    return result
end

function MemStore:addtype (t)
    self.__types [t.name] = deepcopy_no_metatable (t)
end

function MemStore:store (r)
    if not r.uuid then return nil, "no uuid for resource" end
    local new = deepcopy_no_metatable (r)
    if not new.tags then
        new.tags = {}
    end
    self.__resources [r.uuid] = new
    return new
end

local function print_resource (r, pad)
    local write = function (...) io.stderr:write (string.format (...)) end
    local p = pad or ""
    write ("%s%s (", p, r.name)
    if r.size > 1 then
        write ("[%d/%d]", r.size - r.allocated, r.size)
    end
    local t = {}
    for k,v in pairs (r.tags) do
        table.insert (t, k)
    end
    io.stderr:write (table.concat(t, ", "))
    io.stderr:write (")\n")
    for c in r:children () do
        print_resource (c, p.." ")
    end
end

function MemStore:print ()
    print_resource (self:resource ("default"))
end

-- Return a reference to a single resource data table
function MemStore:get (id)
    return self.__resources [id]
end

local function table_empty (t)
    if next(t) == nil then
        return true
    end
    return false
end

-- Unlink child resource [child] from [parent]
local function unlink_child (store, parent, child)
    local name = store:resource_name (child.id)
    local res = store:get (child.id)

    -- remove from parent's children list, remove this hierarchy
    --  from resource hierarchy list
    parent.children[name] = nil
    res.hierarchy [child.hierarchy.name] = nil
    if table_empty (res.hierarchy) then
        store.__resources [res.uuid] = nil
    end

    -- Now unlink all children:
    for n,grandchild in pairs (child.children) do
        unlink_child (store, child, grandchild)
    end
    return true
end

-- Unlink resource ID at uri [arg]
function MemStore:unlink (arg)
    local uri = URI.new (arg)
    if not uri then
        return nil, "bad URI: "..arg
    end

    -- Get a reference to this uri:
    local res = self:get_hierarchy (arg)
    if not res then
        return nil, "Resource "..arg.." not found"
    end

    -- Get a reference to the parent resource:
    local parent = self:get_hierarchy (tostring (uri.parent))
    if not parent then
        return nil, uri.parent .. ": Not found"
    end
    return unlink_child (self, parent, res)
end

local function resource_tag (r, tag, value)
    r.tags [tag] = (value or true)
    return true
end

function MemStore:tag (id, tag, value)
    local r = self:get (id)
    if not r then return r, "not found" end

    if type (tag) == "string" then
        return resource_tag (r, tag, value)
    end

    for k,v in pairs (convert_tags_table (tag)) do
        resource_tag (r, k, v)
    end
    return true
end


function MemStore:delete_tag (id, tag)
    local r = self:get (id)
    if not r then return r, "not found" end
    r.tags[tag] = nil
    return true
end

function MemStore:resource_name (id)
    local r = self:get (id)
    if not r then return nil end
    return r.name .. (r.id or "")
end

function MemStore:ids ()
    local ids = {}
    for id,_ in pairs(self.__resources) do
        table.insert (ids, id)
    end
    return ids
end

-- Link resource id with hierarchy uri
function MemStore:link (resource, hierarchy)
    -- Store this resource if not in DB
    --
    local r = self:get (resource.uuid)
    if not r then
        r = self:store (resource)
    end
    -- Save a reference to [hierarchy] in this resource
    --  by its name
    --
    r.hierarchy [hierarchy.name] = hierarchy.uri
    return r
end

local function ret_error (...)
    return nil, string.format (...)
end

--
-- create a new hierarchy with parent info.name, info.uri
--
function MemStore:hierarchy_create (info, node)

    -- Create new table to contain a hierarchy node:
    --  node = {
    --     id = resource-uuid,
    --     hierarchy = { name = 'name', uri = 'path' }
    --     children = { <list of child nodes > }
    --     parent = link to parent or nil
    --  }
    local n = {
        id = node.resource.uuid,
        hierarchy = {
            name = info.name,
            uri = "/" .. tostring (node.resource)
        },
        children = {}
    }
    -- Prepend the parent uri to this uri
    if info.uri then
        n.hierarchy.uri = info.uri .. n.hierarchy.uri
    end

    -- Setup to append children to this node
    local info = {
        name = info.name,
        uri = n.hierarchy.uri
    }

    -- Add a link to this entry to the resource database
    self:link (node.resource, n.hierarchy)

    -- Add all children recursively
    for _,child in pairs (node.children) do
        local cname = tostring (child.resource)
        local new = self:hierarchy_create (info, child)
        new.parent = n
        n.children [cname] = new
    end
    return n
end

-- Store hierarchy node representation hnode at uri
--  in the current store.
--
-- If uri == "name:/path/to/node" then we try to get
--  'node' as parent of [hnode]. Otherwise, assume
--  [uri] is a new named hierarchy to register and
--  insert the heirarchy in hierarchy[uri].
--
function MemStore:hierarchy_put (uri, hnode)
    if not uri or not hnode then return nil, "invalid args" end
    local uri = URI.new (uri)

    --
    -- If a path to a resource was specified then we attach the
    --  new hierarchy as a child of the existing resource.
    --
    if uri.path then
        local h = self:get_hierarchy (uri.name..":"..uri.path)
        if not h then
            return ret_error ("URI: %s: not found", uri.path)
        end
        local childname = tostring(hnode.resource)
        local info = { uri = uri.path, name = uri.name} 

        -- Link child to parent and parent to child:
        h.children[childname] = self:hierarchy_create (info, hnode)
        h.children[childname].parent = h
        return
    end

    --
    -- No path, so we store hierarchy at hierarchy [uri.name].
    -- For now, error if a hierarchy already exists for this name.
    --
    if self.__hierarchy [uri.name] then
        return ret_error ("Refusing to replace hierarchy object at '%s'", uri)
    end
    local new = self:hierarchy_create ({name = uri.name}, hnode)
    self.__hierarchy [uri.name] =  new
    return new
end

local function find_nearest_parent (store, arg)
    local  uri, err = URI.new (arg)
    if not uri then return nil, err end


    -- Get a reference to the hierarchy by name given by this URI
    local t = store.__hierarchy [uri.name]
    if not t then return nil, uri.name..": doesn't exist" end

    -- If no path component, then return immediately, the top level is
    --  the nearest parent:
    if not uri.path then return t end

    -- Now check each path element in uri until we find a child that
    --  doesn't exist in "store".
    --
    local parent
    for k in uri.path:gmatch("/([^/]+)") do
        -- First iteration we are at top level, e.g. resource
        --  "foo" in default:/foo, just assign parent to to
        --  `t' which points to this resource and continue the loop.
        --
        if not parent then
            parent = t
        else
            -- Otherwise, if the previous parent has no child with
            --  this name, then break and return parent as the
            --  "nearest" parent in this URI:
            --
            if not parent.children[k] then
                break
            end
            -- Update parent to next child and continue
            parent = parent.children[k]
        end
    end
    return parent
end

function MemStore:get_hierarchy (s)
    local  uri, err = URI.new (s)
    if not uri then return nil, err end

    local t = self.__hierarchy [uri.name]
    if not t then return nil, uri.name..": Doesn't exist" end

    if not uri.path then return t end

    local path
    for k,v in uri.path:gmatch("/([^/]+)") do
        if not path then
            -- We are at top level. Since there is no reference to the
            --  top level resource object, check to see if the current
            --  object matches 'k':
            -- Ensure top-level path matches the uri:
            path = k
            if self:resource_name (t.id) ~= path then
                t = nil -- return 'not found' error below
            end
        else
            path = path .. '/' .. k
            t = t.children[k]
        end
        if not t then
            return nil, "path: " ..path.. ": not found"
        end
    end
    return t
end

--
--  Copy a single hierarchy node
--
local function hierarchy_node_copy (n)
    return {
        id = n.id,
        hierarchy = {
            name = n.hierarchy.name,
            uri = n.hierarchy.uri
        },
        children = {},
        parent = n.parent
    }
end

--
--  Copy hierarchy node and children
--
local function hierarchy_node_copy_recursive (n)
    local copy = hierarchy_node_copy (n)
    copy.parent = nil

    for k,v in pairs (n.children) do
        local child = hierarchy_node_copy_recursive (v)
        child.parent = copy
        copy.children [k] = child
    end
    return copy
end

local function hierarchy_validate (t, parent)
    local msg = "hierarchy invalid: "
    local function failure (msg) return nil, "Hierarchy invalid: "..msg end
    if type (t) ~= "table" then
        return failure ("table expected, got "..type(t))
    end
    if not t.id then
        return failure ("resource id missing "..type(t))
    end
    if parent and t.parent ~= parent then
        return failure ("parent reference dangling")
    end
    if type (t.hierarchy) ~= "table" then
        return failure ("hierarchy table missing, got "..type(t.hierarchy))
    end
    if not t.hierarchy.name or not t.hierarchy.uri then
        return failure ("hierarchy table incomplete")
    end
    if type (t.children) ~= "table" then
        return failure ("children table missing, got "..type (t.children))
    end
    for _,v in pairs (t.children) do
        local result, err = hierarchy_validate (v, t)
        if not result then
            return nil, err
        end
    end
    return true
end

local function  h_copy_available (store, hnode)
    if hnode == nil then return nil end
    local r = store:get (hnode.id)
    if r.size == r.allocated then
        return nil
    end

    local new = { children = {} }

    for k,v in pairs (hnode) do
        if k == "children" then
            --
            -- Only copy children with available resources to new node:
            --
            for name,child in pairs (hnode.children) do
                local c = h_copy_available (store, child)
                new.children [name] = c
                if c then c.parent = new end
            end
        else
            new[k] = deepcopy_no_metatable (v)
        end
    end
    return new
end


local function hierarchy_export (self, arg, opts)
    local opts = opts or {}
    local h, err = self:get_hierarchy (arg)
    if not h then return nil, err end

    -- First copy hierarchy at this uri:
    local t = opts.available and h_copy_available (self, h) or deepcopy_no_metatable (h)

    -- Now, traverse up the hierarchy and copy all parents to root
    while t.parent do
        local name = self:resource_name (t.id)
        local parent = hierarchy_node_copy (t.parent)
        parent.children [name] = t
        t.parent = parent
        t = parent
    end

    assert (hierarchy_validate (t))
    return t
end

local function copy_resource (source, dest, id)
    local r = source:get (id)
    if not r then return nil, "copy: Resource "..id.." not found" end
    if not dest:get (id) then
        dest.__resources [id] = deepcopy_no_metatable (r)
    end
    return true
end

-- Copy any missing resourcedata from source to dest objects starting with node
local function dup_resources (source, dest, node)
    local rc, err = copy_resource (source, dest, node.id)
    if not rc then return nil, err end

    assert (node.children)
    for _,child in pairs (node.children) do
        dup_resources (source, dest, child)
    end
    return true
end

-- Merge hierarchy [h2] into hierarchy [h1]
local function hmerge (h1, h2)
    for name, c2 in pairs (h2.children) do
        local c1 = h1.children [name]
        if c1 then
            hmerge (c1, c2)
        else
            h1.children [name] = c2
            c2.parent = h1.children [name]
        end
    end
    return h1
end

-- Merge an exported hierarchy into hierarchy [name] in this store object
function MemStore:merge_exported (name, hierarchy)
    local h = self:get_hierarchy (name)
    if not h then
        self.__hierarchy [name] = hierarchy
    else
        hmerge (self.__hierarchy [name], hierarchy)
    end
    return true
end

function MemStore:merge (uri, name)
    local copy, err = hierarchy_export (self, uri)
    if not copy then return nil, err end

    return self:merge_exported (name, copy)
end

local function node_uri (n)
    return n.hierarchy.name..":"..n.hierarchy.uri
end

local function printf (...)
    io.stdout:write (string.format (...))
end

local function reset_resource_size (store, uuid, n)
    local r = store:get (uuid)
    if n then r.size = n end
    r.allocated = 0
end

--  Copy hierarchy at uri [s] to destination [dst]
function MemStore:copyto (s, dst, n, opts)
    local uri = URI.new (s)
    if not uri then
        return nil, "bad URI: "..arg
    end

    -- Get a reference to the hierarchy node at uri in this repo
    local h = self:get_hierarchy (s)
    local uuid = h.id

    -- Find nearest parent in dst repo to uri.
    local dstparent = find_nearest_parent (dst, s)

    --  If hierarchy doesn't even exist, dstparent is nil. In that
    --   case we copy the entire hierarchy at uri to dst
    if dstparent == nil then
        local h = hierarchy_export (self, s, opts)
        dst:merge_exported (uri.name, h)
        dup_resources (self, dst, h)
        -- Now adjust size of resource at uri to n and reset allocated:
        reset_resource_size (dst, uuid, n)
        return true
    end

    -- if a resource exists already in the dst repo, then
    --  only update the resource and return
    if (node_uri (dstparent) == s) then
        copy_resource (self, dst, dstparent.id)
        reset_resource_size (dst, uuid, n)
        return true
    end

    -- Reverse up tree until we find an existing parent in dst:
    -- Start by copying current node recursively, then we'll add each
    --  parent as we go up the tree...
    local src = h
    local new = hierarchy_node_copy_recursive (src)
    dup_resources (self, dst, new)
    while src.parent and (node_uri (dstparent) ~= node_uri (src.parent)) do
        local name = self:resource_name (src.id)
        src = src.parent
        local t = new
        new = hierarchy_node_copy (src)
        new.children [name] = t
        t.parent = new

        -- copy this resource to dst
        assert (copy_resource (self, dst, src.id))
    end

    assert (node_uri (src.parent) == node_uri (dstparent))

    local name = self:resource_name (src.id)
    dstparent.children [name] = new
    dstparent.children [name].parent = dstparent

    hierarchy_validate (dstparent)

    reset_resource_size (dst, uuid, n)
    return true
end

-- Copy hierarchy at uri given by arg into a new memstore object
function MemStore:copy (arg)
    local copy, err = hierarchy_export (self, arg)
    if not copy then return nil, err end

    -- Create a new, empty memstore object:
    local newstore = new ()
    newstore.__hierarchy [copy.hierarchy.name] = copy
    if not dup_resources (self, newstore, copy) then
        return nil, "Failed to duplicate repo at "..arg
    end
    return newstore
end

function MemStore:dup ()
    local copy = deepcopy_no_metatable (self)
    return setmetatable (copy, MemStore)
end

function MemStore:compare (t2)
    if getmetatable (t2) ~= MemStore then
        return false
    end
    return deepcompare (self, t2)
end

function MemStore:__eq (t2)
    return self:compare (t2)
end

---
-- ResourceAccumulator
--
-- This is a special RDL store object that contains a source db/store
--  and a destination db/store {src, dst}. It inherets all metamethods
--  from MemStore via the destination store [dst], but has one
--  additional operation "add" which enables adding resources from
--  the source [src] repo one by one.
--
-- See the __index metamethod for how the inheritance is done.
--
local ResourceAccumulator = {}
ResourceAccumulator.__index = function (self, key)
    -- First check for methods in this class:
    if ResourceAccumulator [key] then
        return ResourceAccumulator [key]
    end
    --
    -- Otherwise "forward" method request to underlying MemStore
    --  object [dst]
    --
    local fn = self.dst[key]
    if not fn then return nil end
    return function (...)
        if self == ... then -- method call
            return fn (self.dst, select (2, ...))
        else
            return fn (...)
        end
    end
end

function MemStore:resource_accumulator ()
    local ra = {
        src = self,
        dst = new (),
    }
    return (setmetatable (ra, ResourceAccumulator))
end

function ResourceAccumulator:add (id, n, args)

    -- Did we pass in a resource proxy?
    if type(id) == "table" and id.uuid ~= nil then
        id = id.uuid
    end

    local r = self.src:get (id)
    if not r then return nil, "Resource "..id.." not found" end
    --
    -- Add each hierarchy containing resource [r] to destination store:
    -- (Don't delay this operation because resources might be deleted
    --  from the source db)
    --
    for name,path in pairs (r.hierarchy) do
        local uri = name..":"..path
        local rc, err = self.src:copyto (uri, self.dst, n, args)
        if not rc then
            return nil, err
        end
    end
    return true
end

--
-- Return true if any k,v pairs in t2 match a k,v pair in t1
--
local function tags_match (t1, t2)
    for k,v in pairs (t1) do
        if t2[k] and t2[k] == v then
            return true
        end
    end
    return false
end

local function default_find (r, args)

    if args["available"] and r.available == 0 then
        return false
    end

    local T = args.type or args[1]
    if T and T ~= r.type then
        return false
    end

    if args.basename and args.basename ~= r.basename then
        return false
    end

    if args.hostlist and not args.hostlist:find (r.name) then
        return false
    end

    if args.idlist and not (r.id and args.idlist:find (r.id)) then
        return false
    end

    if args.tags and not tags_match (r.tags, args.tags) then
        return false
    end

    return true
end

local function do_find (r, dst, args)
    local T = args.type or args[1]
    local name = args.name
    local fn = args.fn or default_find

    if fn (r, args) then
        dst:add (r.uuid, nil, args)
        return -- No need to traverse further...
    end

    for child in r:children() do
        do_find (child, dst, args)
    end
end

function MemStore:find (arg)
    local hostlist = require 'hostlist'
    local a = self:resource_accumulator ()

    if arg.name then
        -- Always attempt to convert a name to a hostlist:
        local hl, err = hostlist.new (arg.name)
        if not hl then return nil, "find: Invalid name: "..err end
        arg.hostlist = hl
        arg.name = nil
    end

    if arg.ids or arg.id then
        local ids = arg.ids or tostring(arg.id)
        if ids:match("^[0-9]") then ids = "["..ids.."]" end
        local hl, err = hostlist.new (ids)
        if not hl then return nil, "ids: "..err end
        arg.idlist = hl
    end

    if arg.uri then
        do_find (self:resource (arg.uri), a, arg)
    else
        for n,_ in pairs (self.__hierarchy) do
            do_find (self:resource (n), a, arg)
        end
    end

    return a.dst
end


function MemStore:alloc (id, n)
    local r, err = self:get (id)
    if not r then return nil, "Failed to find resource "..id end

    if not n then n = 1 end
    if (r.allocated + n) > r.size then
        return nil, self:resource_name (id) ": Insufficient capacity"
    end
    r.allocated = r.allocated + n
    return n
end

function MemStore:free (id, n)
    local r, err = self:get (id)
    if not r then return nil, "Failed to find resource "..id end

    if not n then n = 1 end
    if r.allocated < n then
        return nil,
          self:resource_name (id) ": Request to free greater than allocated"
    end
    r.allocated = r.allocated - n
    return n
end

--
--  Create a proxy for the resource object [res] stored in memory store
--   [store]
--
local function resource_proxy_create (store, res)
    local resource, err = store:get (res.id)
    if not resource then return nil, err end

    local counter = 0
    local children = {}
    local sort_by_id = function (a,b)
        local r1 = store:get (res.children[a].id)
        local r2 = store:get (res.children[b].id)
        return (r1.id or 0) < (r2.id or 0)
    end

    --  Create table for iterating over named children:
    local function create_child_table ()
        for k,v in pairs (res.children) do
            table.insert (children, k)
        end
    end

    --  Reset counter for iterating children, sorted by id
    --   by default.
    local function reset (self, sortfn)
        if not sortfn then
            sortfn = sort_by_id
        end
        table.sort (children, sortfn)
        counter = 0
    end

    -- Setup: Create child iteration table and reset the counter:
    create_child_table ()
    reset ()

    ---
    -- Iterate over children via built-in counter
    --
    local function next_child()
        counter = counter+1
        if counter <= #children then
            local c = res.children [children [counter]]
            return resource_proxy_create (store, c)
        end
        counter = 0
        return nil
    end

    ---
    -- Unlink child resource with [name] from this hierarchy
    --  and reset counter accordingly
    --
    local function unlink (self, name)
        local child = res.children [name]
        if not child then return nil, "resource not found" end

        -- remove from db:
        unlink_child (store, res, child)

        -- Now need to update local children list:
        for i,v in ipairs (children) do
            if v == name then
                table.remove (children, i)

                -- adjust counter
                if counter >= i then
                    counter = counter - 1
                end
            end
        end
        return true
    end

    local function child_iterator ()
        local i = 0
        local function next_resource (t,index)
            i = i + 1
            local c = res.children [children [i]]
            if not c then return nil end
            return resource_proxy_create (store, c)
        end
        return next_resource, res.children, nil
    end

    ---
    -- Return a table summary of the current resource object
    --
    local function tabulate ()
        return deepcopy_no_metatable (resource)
    end

    ---
    -- Return an aggregation of values for the current resource
    --
    local function aggregate (self)
        return store:aggregate (self.uri)
    end

    ---
    -- Prevent assigning new values to this object:
    --
    local function newindex (self, k, v)
        return nil, "This object cannot be directly indexed"
    end

    ---
    -- Apply a tag to the current resource
    --
    local function tag (self, t, v)
        return store:tag (res.id, t, v)
    end

    local function get_tag (self, t)
        if not resource.tags[t] then
            return nil
        end
        return deepcopy_no_metatable (resource.tags [t])
    end

    local function delete_tag (self, t)
        return store:delete_tag (res.id, t)
    end

    local function alloc (self, n)
        return store:alloc (res.id, n)
    end

    local function free (self, n)
        return store:free (res.id, n)
    end

    ---
    -- Various value accessor functions:
    --
    local function index (self, index)
        if index == "name" then
            return store:resource_name (res.id)
        elseif index == "basename" then
            return resource.name or resource.type
        elseif index == "tags" then
            return deepcopy_no_metatable (resource.tags)
        elseif index == "type" then
            return resource.type
        elseif index == "uri" then
            return res.hierarchy.name..":"..res.hierarchy.uri
        elseif index == "hierarchy_name" then
            return res.hierarchy.name
        elseif index == "path" then
            return res.hierarchy.uri
        elseif index == "id" then
            return resource.id
        elseif index == "uuid" then
            return res.id
        elseif index == "size" then
            return resource.size
        elseif index == "allocated" then
            return resource.allocated
        elseif index == "available" then
            return resource.size - resource.allocated
        end
    end

    local proxy = {
        children = child_iterator,
        next_child = next_child,
        reset = reset,
        tag = tag,
        get = get_tag,
        delete_tag = delete_tag,
        tabulate = tabulate,
        aggregate = aggregate,
        unlink = unlink,
        alloc = alloc,
        free = free,
    }
    return setmetatable (proxy,
        { __index = index,
          __newindex = newindex,
          __tostring = function ()
            return store:resource_name (resource.uuid) end,
         })
end

---
-- Return a resource "proxy" object for URI 'arg' in the store.
--
function MemStore:resource (arg)
    local ref, err = self:get_hierarchy (arg)
    if not ref then return nil, err end
    return resource_proxy_create (self, ref)
end

local function get_aggregate (store, node, result)
    if not result then
        result = {}
    end

    -- First, check to see if there are no available resources for this
    --  node. If so, then this implies children resources are not available
    --  either, so we return immediately, pruning the remainder of the
    --  hierarchy.
    local r = assert (store:get (node.id))
    assert (r.size, "no size for "..require 'inspect'(r))
    if r.allocated == r.size then
        return result
    end

    for _,c in pairs (node.children) do
        get_aggregate (store, c, result)
    end

    local v = r.type
    result [v] = (result [v] or 0) + (r.size - r.allocated)
    return result
end

function MemStore:aggregate (uri)
    local t,err = self:get_hierarchy (uri)
    if not t then return nil, err end

    return get_aggregate (self, t)
end

function MemStore:serialize ()
    return serialize (self)
end

local function bless_memstore (t)
    if not t.__resources or not t.__hierarchy or not t.__types then
        return nil, "Doesn't appear to be a memstore table"
    end
    return setmetatable (t, MemStore)
end

local function load_serialized (s, loader)
    -- TODO: Set up deserialization env:
    --
    local f, err = loader (s)
    if not f then return nil, err end

    local rc, t = pcall (f)
    if not rc then return nil, t end

    return setmetatable (t, MemStore)
end

local function load_file (f)
    local f,err = io.open (f, "r")
    if not f then return nil, err end

    return load_serialized (f, loadfile)
end

local function load_string (s)
    return load_serialized (s, loadstring)
end

return {new = new, bless = bless_memstore}

-- vi: ts=4 sw=4 expandtab
