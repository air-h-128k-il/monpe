/* Dia -- an diagram creation/manipulation program
 * Copyright (C) 1999 Alexander Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include "undo.h"
#include "object_ops.h"
#include "properties-dialog.h"
#include "connectionpoint_ops.h"
#include "focus.h"
#include "group.h"
#include "preferences.h"
#include "diagram_tree_window.h"
#include "textedit.h"
#include "parent.h"
#include "properties.h"
#include "prop_text.h"

#if 0
#define DEBUG_PRINTF(args) printf args
#else
#define DEBUG_PRINTF(args)
#endif

void undo_update_menus(UndoStack *stack);

static void
transaction_point_pointer(Change *change, Diagram *dia)
{
  /* Empty function used to track transactionpoints. */
}

static int
is_transactionpoint(Change *change)
{
  return change->apply == transaction_point_pointer;
}

static Change *
new_transactionpoint(void)
{
  Change *transaction = g_new0(Change, 1);

  if (transaction) {
    transaction->apply = transaction_point_pointer;
    transaction->revert = transaction_point_pointer;
    transaction->free = NULL;
  }
  
  return transaction;
}

UndoStack *
new_undo_stack(Diagram *dia)
{
  UndoStack *stack;
  Change *transaction;
  
  stack = g_new(UndoStack, 1);
  if (stack!=NULL){
    stack->dia = dia;
    transaction = new_transactionpoint();
    transaction->next = transaction->prev = NULL;
    stack->last_change = transaction;
    stack->current_change = transaction;
    stack->last_save = transaction;
    stack->depth = 0;
  }
  return stack;
}

void
undo_destroy(UndoStack *stack)
{
  undo_clear(stack);
  g_free(stack->current_change); /* Free first transaction point. */
  g_free(stack);
}

static void
undo_remove_redo_info(UndoStack *stack)
{
  Change *change;
  Change *next_change;

  DEBUG_PRINTF(("UNDO: Removing redo info\n"));
  
  change = stack->current_change->next;
  stack->current_change->next = NULL;
  stack->last_change = stack->current_change;
    
  while (change != NULL) {
    next_change = change->next;
    if (change->free)
      (change->free)(change);
    g_free(change);
    change = next_change;
  }
  undo_update_menus(stack);
}

void
undo_push_change(UndoStack *stack, Change *change)
{
  if (stack->current_change != stack->last_change)
    undo_remove_redo_info(stack);

  DEBUG_PRINTF(("UNDO: Push new change at %d\n", depth(stack)));
  
  change->prev = stack->last_change;
  change->next = NULL;
  stack->last_change->next = change;
  stack->last_change = change;
  stack->current_change = change;
  undo_update_menus(stack);
}

static void
undo_delete_lowest_transaction(UndoStack *stack)
{
  Change *change;
  Change *next_change;

  /* Find the lowest change: */
  change = stack->current_change;
  while (change->prev != NULL) {
    change = change->prev;
  }

  /* Remove changes from the bottom until (and including)
   * the first transactionpoint.
   * Stop if we reach current_change or NULL.
   */
  do {
    if ( (change == NULL) || (change == stack->current_change))
      break;

    next_change = change->next;
    DEBUG_PRINTF(("freeing one change from the bottom.\n"));
    if (change->free)
      (change->free)(change);
    g_free(change);
    change = next_change;
  } while (!is_transactionpoint(change));
  
  if (is_transactionpoint(change)) {
    stack->depth--;
    DEBUG_PRINTF(("Decreasing stack depth to: %d\n", stack->depth));
  }
  change->prev = NULL;
}

void
undo_set_transactionpoint(UndoStack *stack)
{
  Change *transaction;

  if (is_transactionpoint(stack->current_change))
    return;

  DEBUG_PRINTF(("UNDO: Push new transactionpoint at %d\n", depth(stack)));

  transaction = new_transactionpoint();

  undo_push_change(stack, transaction);
  stack->depth++;
  DEBUG_PRINTF(("Increasing stack depth to: %d\n", stack->depth));

  if (prefs.undo_depth > 0) {
    while (stack->depth > prefs.undo_depth){
      undo_delete_lowest_transaction(stack);
    }
  }
}

void
undo_revert_to_last_tp(UndoStack *stack)
{
  Change *change;
  Change *prev_change;
  
  if (stack->current_change->prev == NULL)
    return; /* Can't revert first transactionpoint */

  change = stack->current_change;
  do {
    prev_change = change->prev;
    (change->revert)(change, stack->dia);
    change = prev_change;
  } while (!is_transactionpoint(change));
  stack->current_change  = change;
  stack->depth--;
  undo_update_menus(stack);
  DEBUG_PRINTF(("Decreasing stack depth to: %d\n", stack->depth));
}

void
undo_apply_to_next_tp(UndoStack *stack)
{
  Change *change;
  Change *next_change;
  
  change = stack->current_change;

  if (change->next == NULL)
    return /* Already at top. */;
  
  do {
    next_change = change->next;
    (change->apply)(change, stack->dia);
    change = next_change;
  } while ( (change != NULL) &&
	    (!is_transactionpoint(change)) );
  if (change == NULL)
    change = stack->last_change;
  stack->current_change = change;
  stack->depth++;
  undo_update_menus(stack);
  DEBUG_PRINTF(("Increasing stack depth to: %d\n", stack->depth));
}


void
undo_clear(UndoStack *stack)
{
  Change *change;

  DEBUG_PRINTF(("undo_clear()\n"));
  
  change = stack->current_change;
    
  while (change->prev != NULL) {
    change = change->prev;
  }
  
  stack->current_change = change;
  stack->depth = 0;
  undo_remove_redo_info(stack);
  undo_update_menus(stack);
}

/** Make updates to menus associated with undo.
 *  Currently just changes sensitivity, but should in the future also
 *  include changing the labels.
 */
void 
undo_update_menus(UndoStack *stack)
{
  ddisplay_do_update_menu_sensitivity(ddisplay_active());
}

/** Marks the undo stack at the time of a save. 
 */
void
undo_mark_save(UndoStack *stack)
{
  stack->last_save = stack->current_change;
}

/** Returns true if the diagram is undo-wise in the same state as at
 * last save, i.e. the current change is the same as it was at last save.
 */
gboolean
undo_is_saved(UndoStack *stack)
{
  return stack->last_save == stack->current_change;
}

/** Returns TRUE if there is an undo or redo item available in the stack.
 * If undo is true, returns TRUE if there's something to undo, otherwise
 * returns TRUE if there's something to redo.
 */
gboolean 
undo_available(UndoStack *stack, gboolean undo)
{
  if (undo) {
    return stack->current_change != NULL;
  } else {
    return stack->current_change != NULL && stack->current_change->next != NULL;
  }
}

/** Remove items from the undo stack until we hit an undo item of a given
 *  type (indicated by its apply function).  Beware that the items are not
 *  just reverted, but totally removed.  This also takes with it all the 
 *  changes above current_change.
 *
 * @param stack The undo stack to remove items from.
 * @param type Indicator of undo type to remove: An apply function.
 * @returns The Change object that stopped the search, if any was found,
 * or NULL otherwise.  In the latter case, the undo stack will be empty.
 */
Change*
undo_remove_to(UndoStack *stack, UndoApplyFunc *type)
{
  Change *current_change = stack->current_change;
  if (current_change == NULL) 
    return NULL;
  while (current_change && current_change->apply != *type) {
    current_change = current_change->prev;
  }
  if (current_change != NULL) {
    stack->current_change = current_change;
    undo_remove_redo_info(stack);
    return current_change;
  } else {
    undo_clear(stack);
    return NULL;
  }
}


/****************************************************************/
/****************************************************************/
/*****************                          *********************/
/***************** Specific undo functions: *********************/
/*****************                          *********************/
/****************************************************************/
/****************************************************************/

/******** Move object list: */

struct MoveObjectsChange {
  Change change;

  Point *orig_pos;
  Point *dest_pos;
  GList *obj_list;
};

static void
move_objects_apply(struct MoveObjectsChange *change, Diagram *dia)
{
  GList *list;
  int i;
  DiaObject *obj;

  object_add_updates_list(change->obj_list, dia);

  list = change->obj_list;
  i=0;
  while (list != NULL) {
    obj = (DiaObject *)  list->data;
    
    obj->ops->move(obj, &change->dest_pos[i]);
    
    list = g_list_next(list); i++;
  }

  list = change->obj_list;
  while (list!=NULL) {
    obj = (DiaObject *) list->data;
    
    diagram_update_connections_object(dia, obj, TRUE);
    
    list = g_list_next(list);
  }

  object_add_updates_list(change->obj_list, dia);
}

static void
move_objects_revert(struct MoveObjectsChange *change, Diagram *dia)
{
  GList *list;
  int i;
  DiaObject *obj;

  object_add_updates_list(change->obj_list, dia);

  list = change->obj_list;
  i=0;
  while (list != NULL) {
    obj = (DiaObject *)  list->data;
    
    obj->ops->move(obj, &change->orig_pos[i]);
    
    list = g_list_next(list); i++;
  }

  list = change->obj_list;
  while (list!=NULL) {
    obj = (DiaObject *) list->data;
    
    diagram_update_connections_object(dia, obj, TRUE);
    
    list = g_list_next(list);
  }

  object_add_updates_list(change->obj_list, dia);
}

static void
move_objects_free(struct MoveObjectsChange *change)
{
  g_free(change->orig_pos);
  g_free(change->dest_pos);
  g_list_free(change->obj_list);
}

extern Change *
undo_move_objects(Diagram *dia, Point *orig_pos, Point *dest_pos,
		  GList *obj_list)
{
  struct MoveObjectsChange *change;

  change = g_new0(struct MoveObjectsChange, 1);
  
  change->change.apply = (UndoApplyFunc) move_objects_apply;
  change->change.revert = (UndoRevertFunc) move_objects_revert;
  change->change.free = (UndoFreeFunc) move_objects_free;

  change->orig_pos = orig_pos;
  change->dest_pos = dest_pos;
  change->obj_list = obj_list;

  DEBUG_PRINTF(("UNDO: Push new move objects at %d\n", depth(dia->undo)));
  undo_push_change(dia->undo, (Change *) change);
  return (Change *)change;
}

/********** Move handle: */

struct MoveHandleChange {
  Change change;

  Point orig_pos;
  Point dest_pos;
  Handle *handle;
  DiaObject *obj;
};

static void
move_handle_apply(struct MoveHandleChange *change, Diagram *dia)
{
  object_add_updates(change->obj, dia);
  change->obj->ops->move_handle(change->obj, change->handle,
				&change->dest_pos, NULL,
				HANDLE_MOVE_USER_FINAL, 0);
  object_add_updates(change->obj, dia);
  diagram_update_connections_object(dia, change->obj, TRUE);
}

static void
move_handle_revert(struct MoveHandleChange *change, Diagram *dia)
{
  object_add_updates(change->obj, dia);
  change->obj->ops->move_handle(change->obj, change->handle,
				&change->orig_pos, NULL,
				HANDLE_MOVE_USER_FINAL, 0);
  object_add_updates(change->obj, dia);
  diagram_update_connections_object(dia, change->obj, TRUE);
}

static void
move_handle_free(struct MoveHandleChange *change)
{
}


Change *
undo_move_handle(Diagram *dia,
		 Handle *handle, DiaObject *obj,
		 Point orig_pos, Point dest_pos)
{
  struct MoveHandleChange *change;

  change = g_new0(struct MoveHandleChange, 1);
  
  change->change.apply = (UndoApplyFunc) move_handle_apply;
  change->change.revert = (UndoRevertFunc) move_handle_revert;
  change->change.free = (UndoFreeFunc) move_handle_free;

  change->orig_pos = orig_pos;
  change->dest_pos = dest_pos;
  change->handle = handle;
  change->obj = obj;

  DEBUG_PRINTF(("UNDO: Push new move handle at %d\n", depth(dia->undo)));

  undo_push_change(dia->undo, (Change *) change);
  return (Change *)change;
}

/***************** Connect object: */

struct ConnectChange {
  Change change;

  DiaObject *obj;
  Handle *handle;
  ConnectionPoint *connectionpoint;
  Point handle_pos;
};

static void
connect_apply(struct ConnectChange *change, Diagram *dia)
{
  object_connect(change->obj, change->handle, change->connectionpoint);
  
  object_add_updates(change->obj, dia);
  change->obj->ops->move_handle(change->obj, change->handle ,
				&change->connectionpoint->pos,
				change->connectionpoint,
				HANDLE_MOVE_CONNECTED, 0);
  
  object_add_updates(change->obj, dia);
}

static void
connect_revert(struct ConnectChange *change, Diagram *dia)
{
  object_unconnect(change->obj, change->handle);
  
  object_add_updates(change->obj, dia);
  change->obj->ops->move_handle(change->obj, change->handle ,
				&change->handle_pos, NULL,
				HANDLE_MOVE_CONNECTED, 0);
  
  object_add_updates(change->obj, dia);
}

static void
connect_free(struct ConnectChange *change)
{
}

extern Change *
undo_connect(Diagram *dia, DiaObject *obj, Handle *handle,
	     ConnectionPoint *connectionpoint)
{
  struct ConnectChange *change;

  change = g_new0(struct ConnectChange, 1);
  
  change->change.apply = (UndoApplyFunc) connect_apply;
  change->change.revert = (UndoRevertFunc) connect_revert;
  change->change.free = (UndoFreeFunc) connect_free;

  change->obj = obj;
  change->handle = handle;
  change->handle_pos = handle->pos;
  change->connectionpoint = connectionpoint;

  DEBUG_PRINTF(("UNDO: Push new connect at %d\n", depth(dia->undo)));
  undo_push_change(dia->undo, (Change *) change);
  return (Change *)change;
}

/*************** Unconnect object: */

struct UnconnectChange {
  Change change;

  DiaObject *obj;
  Handle *handle;
  ConnectionPoint *connectionpoint;
};

static void
unconnect_apply(struct UnconnectChange *change, Diagram *dia)
{
  object_unconnect(change->obj, change->handle);
  
  object_add_updates(change->obj, dia);
}

static void
unconnect_revert(struct UnconnectChange *change, Diagram *dia)
{
  object_connect(change->obj, change->handle, change->connectionpoint);
  
  object_add_updates(change->obj, dia);
}

static void
unconnect_free(struct UnconnectChange *change)
{
}

extern Change *
undo_unconnect(Diagram *dia, DiaObject *obj, Handle *handle)
{
  struct UnconnectChange *change;

  change = g_new0(struct UnconnectChange, 1);
  
  change->change.apply = (UndoApplyFunc) unconnect_apply;
  change->change.revert = (UndoRevertFunc) unconnect_revert;
  change->change.free = (UndoFreeFunc) unconnect_free;

  change->obj = obj;
  change->handle = handle;
  change->connectionpoint = handle->connected_to;

  DEBUG_PRINTF(("UNDO: Push new unconnect at %d\n", depth(dia->undo)));
  undo_push_change(dia->undo, (Change *) change);
  return (Change *)change;
}


/******** Delete object list: */

struct DeleteObjectsChange {
  Change change;

  Layer *layer;
  GList *obj_list; /* Owning reference when applied */
  GList *original_objects;
  int applied;
};

static void
delete_objects_apply(struct DeleteObjectsChange *change, Diagram *dia)
{
  GList *list;
  
  DEBUG_PRINTF(("delete_objects_apply()\n"));
  change->applied = 1;
  diagram_unselect_objects(dia, change->obj_list);
  layer_remove_objects(change->layer, change->obj_list);
  object_add_updates_list(change->obj_list, dia);
  
  list = change->obj_list;
  while (list != NULL) {
    DiaObject *obj = (DiaObject *)list->data;

  /* Have to hide any open properties dialog
     if it contains some object in cut_list */
    properties_hide_if_shown(dia, obj);

    if (obj->parent) /* Lose references to deleted object */
      obj->parent->children = g_list_remove(obj->parent->children, obj);

    /* unregister object from node */
    if (obj->node != NULL &&
        dtree_is_valid_node(DIA_DIAGRAM_DATA(dia)->dtree,obj->node)) {
      dnode_unset_object(obj->node,obj);
    }
    list = g_list_next(list);
  }
  diagram_tree_remove_objects(diagram_tree(), change->obj_list);
}

static void
delete_objects_revert(struct DeleteObjectsChange *change, Diagram *dia)
{
  GList *list,*_list;

  DEBUG_PRINTF(("delete_objects_revert()\n"));
  change->applied = 0;

  _list = NULL;
  list = change->obj_list;
  while (list)
  {
    DiaObject *obj = (DiaObject *) list->data;
    Property *prop = object_prop_by_name_type(obj,"embed_id",PROP_TYPE_STRING);
    if (prop != NULL) {
      gchar *embed_id = ((StringProperty*)prop)->string_data;
      obj->node = dtree_set_data_path(DIA_DIAGRAM_DATA(dia)->dtree,
        embed_id,obj);
      if (obj->node != NULL) {
        _list = g_list_append(_list,obj);
      }
    } else {
      _list = g_list_append(_list,obj);
    }
    list = g_list_next(list);
  }
/*FIXME*/
#if 0
  g_list_free(change->obj_list);
#endif
  change->obj_list = _list;

  layer_set_object_list(change->layer,
			g_list_copy(change->original_objects));
  object_add_updates_list(change->obj_list, dia);

  list = change->obj_list;
  while (list)
  {
    DiaObject *obj = (DiaObject *) list->data;
    if (obj->parent) /* Restore child references */
    	obj->parent->children = g_list_append(obj->parent->children, obj);
    	
    /* Emit a signal per object reverted */
    data_emit(layer_get_parent_diagram(change->layer),change->layer,obj,"object_add");
 
    list = g_list_next(list);
  }

  diagram_tree_add_objects(diagram_tree(), dia, change->obj_list);
}

static void
delete_objects_free(struct DeleteObjectsChange *change)
{
  DEBUG_PRINTF(("delete_objects_free()\n"));
  if (change->applied)
    destroy_object_list(change->obj_list);
  else
    g_list_free(change->obj_list);
  g_list_free(change->original_objects);
}

/*
  This function deletes specified objects along with any children
  they might have.
  undo_delete_objects() only deletes the objects that are specified.
*/
Change *
undo_delete_objects_children(Diagram *dia, GList *obj_list)
{
  return undo_delete_objects(dia, parent_list_affected(obj_list));
}

Change *
undo_delete_objects(Diagram *dia, GList *obj_list)
{
  struct DeleteObjectsChange *change;

  change = g_new0(struct DeleteObjectsChange, 1);
  
  change->change.apply = (UndoApplyFunc) delete_objects_apply;
  change->change.revert = (UndoRevertFunc) delete_objects_revert;
  change->change.free = (UndoFreeFunc) delete_objects_free;

  change->layer = dia->data->active_layer;
  change->obj_list = obj_list;
  change->original_objects = g_list_copy(dia->data->active_layer->objects);
  change->applied = 0;

  DEBUG_PRINTF(("UNDO: Push new delete objects at %d\n", depth(dia->undo)));
  undo_push_change(dia->undo, (Change *) change);
  return (Change *)change;
}

/******** Insert object list: */

struct InsertObjectsChange {
  Change change;

  Layer *layer;
  GList *obj_list; /* Owning reference when not applied */
  int applied;
};

static void
insert_objects_apply(struct InsertObjectsChange *change, Diagram *dia)
{
  GList *list,*_list;

  DEBUG_PRINTF(("insert_objects_apply()\n"));
  change->applied = 1;

  _list = NULL;
  list = change->obj_list;
  while (list)
  {
    DiaObject *obj = (DiaObject *) list->data;
    Property *prop = object_prop_by_name_type(obj,"embed_id",PROP_TYPE_STRING);
    if (prop != NULL) {
      gchar *embed_id = ((StringProperty*)prop)->string_data;
      obj->node = dtree_set_data_path(DIA_DIAGRAM_DATA(dia)->dtree,
        embed_id,obj);
      if (obj->node != NULL) {
        _list = g_list_append(_list,obj);
      }
    } else {
      _list = g_list_append(_list,obj);
    }
    list = g_list_next(list);
  }
/* FIXME this line cause SEGV*/
#if 0
  g_list_free(change->obj_list);
#endif
  change->obj_list = _list;

  if (change->obj_list == NULL) {
    return ;
  }

  layer_add_objects(change->layer, g_list_copy(change->obj_list));
  object_add_updates_list(change->obj_list, dia);
  diagram_tree_add_objects(diagram_tree(), dia, change->obj_list);
}

static void
insert_objects_revert(struct InsertObjectsChange *change, Diagram *dia)
{
  GList *list;
  
  DEBUG_PRINTF(("insert_objects_revert()\n"));
  change->applied = 0;
  diagram_unselect_objects(dia, change->obj_list);
  layer_remove_objects(change->layer, change->obj_list);
  object_add_updates_list(change->obj_list, dia);
  
  list = change->obj_list;
  while (list != NULL) {
    DiaObject *obj = (DiaObject *)list->data;
    if (obj->node != NULL && 
        dtree_is_valid_node(DIA_DIAGRAM_DATA(dia)->dtree,obj->node)) {
      dnode_unset_object(obj->node,obj);
    }

  /* Have to hide any open properties dialog
     if it contains some object in cut_list */
    properties_hide_if_shown(dia, obj);

    list = g_list_next(list);
  }

  diagram_tree_remove_objects(diagram_tree(), change->obj_list);
}

static void
insert_objects_free(struct InsertObjectsChange *change)
{
  DEBUG_PRINTF(("insert_objects_free()\n"));
  if (!change->applied)
    destroy_object_list(change->obj_list);
  else
    g_list_free(change->obj_list);
}

Change *
undo_insert_objects(Diagram *dia, GList *obj_list, int applied)
{
  struct InsertObjectsChange *change;

  change = g_new0(struct InsertObjectsChange, 1);
  
  change->change.apply = (UndoApplyFunc) insert_objects_apply;
  change->change.revert = (UndoRevertFunc) insert_objects_revert;
  change->change.free = (UndoFreeFunc) insert_objects_free;

  change->layer = dia->data->active_layer;
  change->obj_list = obj_list;
  change->applied = applied;

  DEBUG_PRINTF(("UNDO: Push new insert objects at %d\n", depth(dia->undo)));
  undo_push_change(dia->undo, (Change *) change);
  return (Change *)change;
}

/******** Reorder object list: */

struct ReorderObjectsChange {
  Change change;

  Layer *layer;
  GList *changed_list; /* Owning reference when applied */
  GList *original_objects;
  GList *reordered_objects;
};

static void
reorder_objects_apply(struct ReorderObjectsChange *change, Diagram *dia)
{
  DEBUG_PRINTF(("reorder_objects_apply()\n"));
  layer_set_object_list(change->layer,
			g_list_copy(change->reordered_objects));
  object_add_updates_list(change->changed_list, dia);
}

static void
reorder_objects_revert(struct ReorderObjectsChange *change, Diagram *dia)
{
  DEBUG_PRINTF(("reorder_objects_revert()\n"));
  layer_set_object_list(change->layer,
			g_list_copy(change->original_objects));
  object_add_updates_list(change->changed_list, dia);
}

static void		
reorder_objects_free(struct ReorderObjectsChange *change)
{			    
  DEBUG_PRINTF(("reorder_objects_free()\n"));
  g_list_free(change->changed_list);
  g_list_free(change->original_objects);
  g_list_free(change->reordered_objects);
}

Change *
undo_reorder_objects(Diagram *dia, GList *changed_list, GList *orig_list)
{
  struct ReorderObjectsChange *change;

  change = g_new0(struct ReorderObjectsChange, 1);
  
  change->change.apply = (UndoApplyFunc) reorder_objects_apply;
  change->change.revert = (UndoRevertFunc) reorder_objects_revert;
  change->change.free = (UndoFreeFunc) reorder_objects_free;

  change->layer = dia->data->active_layer;
  change->changed_list = changed_list;
  change->original_objects = orig_list;
  change->reordered_objects = g_list_copy(dia->data->active_layer->objects);

  DEBUG_PRINTF(("UNDO: Push new reorder objects at %d\n", depth(dia->undo)));
  undo_push_change(dia->undo, (Change *) change);
  return (Change *)change;
}

/******** ObjectChange: */

struct ObjectChangeChange {
  Change change;

  DiaObject *obj;
  ObjectChange *obj_change;
};


static void
object_change_apply(struct ObjectChangeChange *change,
		    Diagram *dia)
{
  object_add_updates(change->obj, dia);
  change->obj_change->apply(change->obj_change, change->obj);
  { /* Make sure object updates its data: */
    Point p = change->obj->position;
    (change->obj->ops->move)(change->obj,&p);
  }
  object_add_updates(change->obj, dia);
  
  diagram_update_connections_object(dia, change->obj, TRUE);

  properties_update_if_shown(dia, change->obj);
}

static void
object_change_revert(struct ObjectChangeChange *change,
		     Diagram *dia)
{
  object_add_updates(change->obj, dia);
  change->obj_change->revert(change->obj_change, change->obj);
  { /* Make sure object updates its data: */
    Point p = change->obj->position;
    (change->obj->ops->move)(change->obj,&p);
  }
  object_add_updates(change->obj, dia);
  
  diagram_update_connections_object(dia, change->obj, TRUE);

  properties_update_if_shown(dia, change->obj);
}

static void
object_change_free(struct ObjectChangeChange *change)
{
  DEBUG_PRINTF(("state_change_free()\n"));
  if (change->obj_change->free)
    (*change->obj_change->free)(change->obj_change);
  g_free(change->obj_change);
}

Change *
undo_object_change(Diagram *dia, DiaObject *obj,
		   ObjectChange *obj_change)
{
  struct ObjectChangeChange *change;

  change = g_new0(struct ObjectChangeChange, 1);
  
  change->change.apply = (UndoApplyFunc) object_change_apply;
  change->change.revert = (UndoRevertFunc) object_change_revert;
  change->change.free = (UndoFreeFunc) object_change_free;

  change->obj = obj;
  change->obj_change = obj_change;

  DEBUG_PRINTF(("UNDO: Push new obj_change at %d\n", depth(dia->undo)));
  undo_push_change(dia->undo, (Change *) change);
  return (Change *)change;
}

/******** Group object list: */

/** Grouping and ungrouping are two subtly different changes:  While 
 * ungrouping preserves the front/back position, grouping cannot do that,
 * since the act of grouping destroys positions for the members of the
 * group, and those positions have to be restored in the undo.
 */

struct GroupObjectsChange {
  Change change;

  Layer *layer;
  DiaObject *group;   /* owning reference if not applied */
  GList *obj_list; /* The list of objects in this group.  Owned by the
		      group */
  GList *orig_list; /* A copy of the original list of all objects,
		       from before the group was created.  Owned by
		       the group */
  int applied;
};

static void
group_objects_apply(struct GroupObjectsChange *change, Diagram *dia)
{
  GList *list;

  DEBUG_PRINTF(("group_objects_apply()\n"));
  
  change->applied = 1;
  
  diagram_unselect_objects(dia, change->obj_list);
  layer_remove_objects(change->layer, change->obj_list);
  layer_add_object(change->layer, change->group);
  object_add_updates(change->group, dia);

  list = change->obj_list;
  while (list != NULL) {
    DiaObject *obj = (DiaObject *)list->data;
    
  /* Have to hide any open properties dialog
     if it contains some object in cut_list */
    properties_hide_if_shown(dia, obj);

    object_add_updates(obj, dia);

    list = g_list_next(list);
  }

  diagram_tree_add_object(diagram_tree(), dia, change->group);
  diagram_tree_remove_objects(diagram_tree(), change->obj_list);
}

static void
group_objects_revert(struct GroupObjectsChange *change, Diagram *dia)
{
  DEBUG_PRINTF(("group_objects_revert()\n"));
  change->applied = 0;
  
  diagram_unselect_object(dia, change->group);
  object_add_updates(change->group, dia);

  layer_set_object_list(change->layer, g_list_copy(change->orig_list));
  
  object_add_updates_list(change->obj_list, dia);

  properties_hide_if_shown(dia, change->group);

  diagram_tree_add_objects(diagram_tree(), dia, change->obj_list);
  diagram_tree_remove_object(diagram_tree(), change->group);
}

static void
group_objects_free(struct GroupObjectsChange *change)
{
  DEBUG_PRINTF(("group_objects_free()\n"));
  if (!change->applied) {
    group_destroy_shallow(change->group);
    change->group = NULL;
    change->obj_list = NULL;
    /** Leak here? */
  }
  g_list_free(change->orig_list);
}

Change *
undo_group_objects(Diagram *dia, GList *obj_list, DiaObject *group,
		   GList *orig_list)
{
  struct GroupObjectsChange *change;

  change = g_new0(struct GroupObjectsChange, 1);
  
  change->change.apply = (UndoApplyFunc) group_objects_apply;
  change->change.revert = (UndoRevertFunc) group_objects_revert;
  change->change.free = (UndoFreeFunc) group_objects_free;

  change->layer = dia->data->active_layer;
  change->group = group;
  change->obj_list = obj_list;
  change->orig_list = orig_list;
  change->applied = 1;

  DEBUG_PRINTF(("UNDO: Push new group objects at %d\n", depth(dia->undo)));
  undo_push_change(dia->undo, (Change *) change);
  return (Change *)change;
}

/******** Ungroup object list: */

struct UngroupObjectsChange {
  Change change;

  Layer *layer;
  DiaObject *group;   /* owning reference if applied */
  GList *obj_list; /* This list is owned by the ungroup. */
  int group_index;
  int applied;
};

static void
ungroup_objects_apply(struct UngroupObjectsChange *change, Diagram *dia)
{
  DEBUG_PRINTF(("ungroup_objects_apply()\n"));
  
  change->applied = 1;
  
  diagram_unselect_object(dia, change->group);
  object_add_updates(change->group, dia);
  layer_replace_object_with_list(change->layer, change->group,
				 g_list_copy(change->obj_list));
  object_add_updates_list(change->obj_list, dia);
  
  properties_hide_if_shown(dia, change->group);

  diagram_tree_add_objects(diagram_tree(), dia, change->obj_list);
  diagram_tree_remove_object(diagram_tree(), change->group);
}

static void
ungroup_objects_revert(struct UngroupObjectsChange *change, Diagram *dia)
{
  GList *list;
  
  DEBUG_PRINTF(("ungroup_objects_revert()\n"));
  change->applied = 0;
  

  diagram_unselect_objects(dia, change->obj_list);
  layer_remove_objects(change->layer, change->obj_list);
  layer_add_object_at(change->layer, change->group, change->group_index);
  object_add_updates(change->group, dia);

  list = change->obj_list;
  while (list != NULL) {
    DiaObject *obj = (DiaObject *)list->data;
    
  /* Have to hide any open properties dialog
     if it contains some object in cut_list */
    properties_hide_if_shown(dia, obj);

    list = g_list_next(list);
  }

  diagram_tree_add_object(diagram_tree(), dia, change->group);
  diagram_tree_remove_objects(diagram_tree(), change->obj_list);
}

static void
ungroup_objects_free(struct UngroupObjectsChange *change)
{
  DEBUG_PRINTF(("ungroup_objects_free()\n"));
  if (change->applied) {
    group_destroy_shallow(change->group);
    change->group = NULL;
    change->obj_list = NULL;
  }
}

Change *
undo_ungroup_objects(Diagram *dia, GList *obj_list, DiaObject *group,
		     int group_index)
{
  struct UngroupObjectsChange *change;

  change = g_new0(struct UngroupObjectsChange, 1);
  
  change->change.apply = (UndoApplyFunc) ungroup_objects_apply;
  change->change.revert = (UndoRevertFunc) ungroup_objects_revert;
  change->change.free = (UndoFreeFunc) ungroup_objects_free;

  change->layer = dia->data->active_layer;
  change->group = group;
  change->obj_list = obj_list;
  change->group_index = group_index;
  change->applied = 1;

  DEBUG_PRINTF(("UNDO: Push new ungroup objects at %d\n", depth(dia->undo)));
  undo_push_change(dia->undo, (Change *) change);
  return (Change *)change;
}

/******* PARENTING */

struct ParentChange {
  Change change;
  DiaObject *parentobj, *childobj;
  gboolean parent;
};

/** Performs the actual parenting of a child to a parent.
 * Since no display changes arise from this, we need call no update. */
static void
parent_object(Diagram *dia, DiaObject *parent, DiaObject *child)
{
  child->parent = parent;
  parent->children = g_list_prepend(parent->children, child);
}

/** Performs the actual removal of a child from a parent.
 * Since no display changes arise from this, we need call no update. */
static void
unparent_object(Diagram *dia, DiaObject *parent, DiaObject *child)
{
  child->parent = NULL;
  parent->children = g_list_remove(parent->children, child);
}

/** Applies the given ParentChange */
static void
parent_change_apply(Change *change, Diagram *dia)
{
  struct ParentChange *parentchange = (struct ParentChange*)change;
  if (parentchange->parent) {
    parent_object(dia, parentchange->parentobj, parentchange->childobj);
  } else {
    unparent_object(dia, parentchange->parentobj, parentchange->childobj);
  }
}

/** Reverts the given ParentChange */
static void
parent_change_revert(Change *change, Diagram *dia)
{
  struct ParentChange *parentchange = (struct ParentChange*)change;
  if (!parentchange->parent) {
    parent_object(dia, parentchange->parentobj, parentchange->childobj);
  } else {
    unparent_object(dia, parentchange->parentobj, parentchange->childobj);
  }
}

/** Frees items in the change -- none really */
static void
parent_change_free(Change *change)
{
}

/** Create a new Change object for parenting/unparenting.
 * `parent' is TRUE if applying this change makes childobj a
 * child of parentobj.
 */
Change *
undo_parenting(Diagram *dia, DiaObject* parentobj, DiaObject* childobj,
	       gboolean parent)
{
  struct ParentChange *parentchange = g_new0(struct ParentChange, 1);
  Change *change = (Change*)parentchange;
  change->apply = parent_change_apply;
  change->revert = parent_change_revert;
  change->free = parent_change_free;

  parentchange->parentobj = parentobj;
  parentchange->childobj = childobj;
  parentchange->parent = parent;

  DEBUG_PRINTF(("UNDO: Push new obj_change at %d\n", depth(dia->undo)));
  undo_push_change(dia->undo, change);
  return change;
}

/* ********* MOVE TO OTHER LAYER  */
typedef struct _MoveObjectToLayerChange {
  Change change;
  /** The objects we are moving */
  GList *objects;
  /** All objects in the original layer */
  GList *orig_list;
  /** The active layer when started */
  Layer *orig_layer;
  gboolean moving_up;
} MoveObjectToLayerChange;

/*!
 * BEWARE: we need to notify the DiagramTree somehow - maybe 
 * better make it listen to object-add signal?
 */
static void 
move_object_layer_relative(Diagram *dia, GList *objects, gint dist)
{
  /* from the active layer to above or below */
  Layer *active, *target;
  guint pos;
 
  g_return_if_fail(dia->data->active_layer);

  active =  dia->data->active_layer;
  for (pos = 0; pos < dia->data->layers->len; ++pos)
    if (active == g_ptr_array_index(dia->data->layers, pos))
      break;

  pos = (pos + dia->data->layers->len + dist) % dia->data->layers->len;
  target = g_ptr_array_index(dia->data->layers, pos);
  object_add_updates_list(objects, dia);
  layer_remove_objects(active, objects);
  diagram_tree_add_objects(diagram_tree(), dia, objects);
  layer_add_objects(target, g_list_copy(objects));
  data_set_active_layer(dia->data, target);
  diagram_tree_add_objects(diagram_tree(), dia, objects);
}

static void
move_object_to_layer_apply(MoveObjectToLayerChange *change, Diagram *dia)
{
  if (change->moving_up) {
    move_object_layer_relative(dia, change->objects, 1);
  } else {
    move_object_layer_relative(dia, change->objects, -1);
  }
}

static void
move_object_to_layer_revert(MoveObjectToLayerChange *change, Diagram *dia)
{
  if (change->moving_up) {
    move_object_layer_relative(dia, change->objects, -1);
  } else {
    diagram_unselect_objects(dia, change->objects);
    move_object_layer_relative(dia, change->objects, 1);
    /* overwriting the 'unsorted' list of objects to the order it had before */
    layer_set_object_list(change->orig_layer, g_list_copy(change->orig_list));
    object_add_updates_list(change->orig_list, dia);
  }
}

static void
move_object_to_layer_free(MoveObjectToLayerChange *change)
{
  g_list_free(change->objects);
  g_list_free(change->orig_list);
  change->objects = NULL;
  change->orig_list = NULL;
}

Change *
undo_move_object_other_layer(Diagram *dia, GList *selected_list,
			     gboolean moving_up)
{
  MoveObjectToLayerChange *movetolayerchange 
    = g_new0(MoveObjectToLayerChange, 1);
  Change *change = (Change*)movetolayerchange;
  change->apply = (UndoApplyFunc) move_object_to_layer_apply;
  change->revert = (UndoRevertFunc) move_object_to_layer_revert;
  change->free = (UndoFreeFunc) move_object_to_layer_free;

  movetolayerchange->orig_layer = dia->data->active_layer;
  movetolayerchange->orig_list = g_list_copy(dia->data->active_layer->objects);
  movetolayerchange->objects = g_list_copy(selected_list);
  movetolayerchange->moving_up = moving_up;

  DEBUG_PRINTF(("UNDO: Push new obj_layer_change at %d\n", depth(dia->undo)));
  undo_push_change(dia->undo, change);
  return change;
}
