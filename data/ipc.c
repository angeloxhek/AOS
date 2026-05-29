#include <kernel/internal.h>

// -------------------------
//           IPC
// -------------------------

int64_t ipc_forward(uint64_t dest_pid, message_t* user_msg) {
    uint64_t irq = hal_irq_save();
    
    process_t* target = get_process_by_id(dest_pid);
    if (!target) { 
        hal_irq_restore(irq); 
        return SYS_RES_INVALID; 
    }
    
    msg_node_t* node = (msg_node_t*)kernel_malloc(sizeof(msg_node_t));
    if (!node) { 
        hal_irq_restore(irq); 
        return SYS_RES_KERNEL_ERR; 
    }
    kernel_memset(node, 0, sizeof(msg_node_t));
    
    node->msg = *user_msg;
    node->next = 0;
    
    if (target->msg_queue_tail) {
        target->msg_queue_tail->next = node;
    } else {
        target->msg_queue_head = node;
    }
    target->msg_queue_tail = node;
	
	uint64_t count = 0;
	get_thread_list(dest_pid, 0, &count);
	
	uint64_t* buffer = (uint64_t*)kernel_malloc(count*sizeof(uint64_t));
	if (!buffer) {
		hal_irq_restore(irq);
		return SYS_RES_OK;
	}
	kernel_memset(buffer, 0, count*sizeof(uint64_t));
	
	get_thread_list(dest_pid, buffer, &count);
	
	for (uint64_t c = 0; c < count; c++) {
		thread_t* th = get_thread_by_id(buffer[c]);
		if (!th) continue;
		
		if (th->fs_base != 0) {
			uint64_t old_space = hal_get_current_address_space();
			if (old_space != th->cr3) {
				hal_set_current_address_space(th->cr3);
			}
			
			aos_tcb_t* tcb = (aos_tcb_t*)th->fs_base;
			tcb->pending_msgs++;
			
			if (old_space != th->cr3) {
				hal_set_current_address_space(old_space);
			}
		}
		
		if (th->state == THREAD_BLOCKED && th->waiting_for_msg) {
			th->state = THREAD_READY;
			th->waiting_for_msg = 0;
		}
	}
    
    hal_irq_restore(irq);
    return SYS_RES_OK;
}

int64_t ipc_requeue(message_t* user_msg) {
    return ipc_forward(current_thread->owner->id, user_msg);
}

int64_t ipc_send(uint64_t dest_pid, message_t* user_msg) {
    user_msg->sender_pid = current_thread->owner->id;
    return ipc_forward(dest_pid, user_msg);
}

static int __ipc_pop_msg(message_t* out_msg) {
    if (!current_thread->owner->msg_queue_head) return -1;
    msg_node_t* node = current_thread->owner->msg_queue_head;
    current_thread->owner->msg_queue_head = node->next;
    
    if (!current_thread->owner->msg_queue_head) current_thread->owner->msg_queue_tail = 0;
    
    if (current_thread->fs_base != 0) {
        aos_tcb_t* tcb = (aos_tcb_t*)current_thread->fs_base;
        if (tcb->pending_msgs > 0) tcb->pending_msgs--;
    }
    
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

int64_t ipc_receive_ex(uint64_t pid, msg_type_t type, msg_subtype_t subtype, message_t* out_msg) {
    while (1) {
        message_t temp_msg;
        int64_t res = ipc_receive(&temp_msg);
        if (res != SYS_RES_OK) return res;
        
        int match = 1;
        if (pid != 0 && temp_msg.sender_pid != pid) match = 0;
        if (type != MSG_TYPE_NONE && temp_msg.type != type) match = 0;
        if (subtype != MSG_SUBTYPE_NONE && temp_msg.subtype != subtype) match = 0;
        
        if (match) {
            *out_msg = temp_msg;
            return SYS_RES_OK;
        }
        
        ipc_requeue(&temp_msg);
        schedule();
    }
}