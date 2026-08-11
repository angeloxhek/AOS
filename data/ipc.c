#include <kernel/internal.h>

// -------------------------
//           IPC
// -------------------------

int64_t ipc_forward(apid_t dest_pid, message_t* user_msg) {
    msg_node_t* node = (msg_node_t*)kernel_malloc(sizeof(msg_node_t));
    if (!node) return SYS_RES_KERNEL_ERR; 
    kernel_memset(node, 0, sizeof(msg_node_t));
    node->msg = *user_msg;
    
    uint64_t irq = hal_irq_save();
    
    process_t* target = get_process_by_id(dest_pid);
    if (!target) { 
        hal_irq_restore(irq); 
        kernel_free(node); 
        return SYS_RES_INVALID; 
    }
    
    if (target->msg_queue_tail) target->msg_queue_tail->next = node;
    else target->msg_queue_head = node;
    target->msg_queue_tail = node;
    
	if (ready_queue) {
        thread_t* th = ready_queue;
        do {
            if (th->owner != 0 && th->owner->id == dest_pid) {
                if (th->state == THREAD_BLOCKED && th->waiting_for_msg) {
                    th->state = THREAD_READY;
                    th->waiting_for_msg = 0;
                    break; 
                }
            }
            th = th->next;
        } while (th && th != ready_queue);
    }

    if (target->peb_phys_page != 0) {
        void* kvirt = temp_map(target->peb_phys_page);
        aos_peb_t* peb = (aos_peb_t*)kvirt;
        
        peb->pending_msgs++;
        
        temp_unmap(kvirt);
    }
	
	hal_irq_restore(irq);
    
    return SYS_RES_OK;
}

int64_t ipc_requeue(message_t* user_msg) {
    return ipc_forward(current_thread->owner->id, user_msg);
}

int64_t ipc_send(apid_t dest_pid, message_t* user_msg) {
    user_msg->sender_pid = current_thread->owner->id;
    return ipc_forward(dest_pid, user_msg);
}

static int __ipc_pop_msg(message_t* out_msg) {
    if (!current_thread->owner->msg_queue_head) return -1;
    msg_node_t* node = current_thread->owner->msg_queue_head;
    current_thread->owner->msg_queue_head = node->next;
    
    if (!current_thread->owner->msg_queue_head) current_thread->owner->msg_queue_tail = 0;
    
    /*if (current_thread->fs_base != 0) {
        aos_tcb_t* tcb = (aos_tcb_t*)current_thread->fs_base;
        if (tcb->pending_msgs > 0) tcb->pending_msgs--;
    }*/
    
    *out_msg = node->msg;
    kernel_free(node);
    return 0;
}

int64_t ipc_try_receive(message_t* out_msg) {
    uint64_t irq = hal_irq_save();
    int res = __ipc_pop_msg(out_msg);
    hal_irq_restore(irq);
    return res ? SYS_RES_QUEUE_EMPTY : SYS_RES_OK;
}

int64_t ipc_receive(message_t* out_msg) {
    while (1) {
        uint64_t irq = hal_irq_save();
        if (!__ipc_pop_msg(out_msg)) {
            hal_irq_restore(irq);
            return SYS_RES_OK;
        }
        current_thread->waiting_for_msg = 1;
        current_thread->state = THREAD_BLOCKED;
        schedule();
        hal_irq_restore(irq);
    }
}

int64_t ipc_receive_ex(apid_t pid, msg_type_t type, msg_subtype_t subtype, message_t* out_msg) {
    while (1) {
        uint64_t irq = hal_irq_save();
        
        msg_node_t* curr = current_thread->owner->msg_queue_head;
        msg_node_t* prev = 0;
        
        while (curr) {
            int match = 1;
            if (pid != 0 && curr->msg.sender_pid != pid) match = 0;
            if (type != MSG_TYPE_NONE && curr->msg.type != type) match = 0;
            if (subtype != MSG_SUBTYPE_NONE && curr->msg.subtype != subtype) match = 0;
            
            if (match) {
                *out_msg = curr->msg;
                if (prev) prev->next = curr->next;
                else current_thread->owner->msg_queue_head = curr->next;
                
                if (curr == current_thread->owner->msg_queue_tail) {
                    current_thread->owner->msg_queue_tail = prev;
                }
                
                kernel_free(curr);
                hal_irq_restore(irq);
                return SYS_RES_OK;
            }
            prev = curr;
            curr = curr->next;
        }
        current_thread->waiting_for_msg = 1;
        current_thread->state = THREAD_BLOCKED;
        schedule();
        hal_irq_restore(irq);
    }
}