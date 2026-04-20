#include "include/kernel_internal.h"

// -------------------------
//           IPC
// -------------------------

int64_t ipc_send(uint64_t dest_tid, message_t* user_msg) {
    asm volatile("cli");
    thread_t* target = get_thread_by_id(dest_tid);
    if (!target) { asm volatile("sti"); return SYS_RES_INVALID; }
    msg_node_t* node = (msg_node_t*)kernel_malloc(sizeof(msg_node_t));
    if (!node) { asm volatile("sti"); return SYS_RES_KERNEL_ERR; }
    kernel_memset(node, 0, sizeof(msg_node_t));
    node->msg = *user_msg;
    node->msg.sender_tid = current_thread->tid;
    node->next = 0;
    if (target->msg_queue_tail) {
        target->msg_queue_tail->next = node;
    } else {
        target->msg_queue_head = node;
    }
    target->msg_queue_tail = node;
    if (target->fs_base != 0) {
        uint64_t old_cr3 = get_current_pml4();
        if (old_cr3 != target->cr3) {
            set_current_pml4(target->cr3);
        }
        aos_tcb_t* tcb = (aos_tcb_t*)target->fs_base;
        tcb->pending_msgs++;
        if (old_cr3 != target->cr3) {
            set_current_pml4(old_cr3);
        }
    }
    if (target->state == THREAD_BLOCKED && target->waiting_for_msg) {
        target->state = THREAD_READY;
        target->waiting_for_msg = 0;
    }
    asm volatile("sti");
    return SYS_RES_OK;
}

static int __ipc_pop_msg(message_t* out_msg) {
	if (!current_thread->msg_queue_head) return -1;
	msg_node_t* node = current_thread->msg_queue_head;
	current_thread->msg_queue_head = node->next;
	if (!current_thread->msg_queue_head) current_thread->msg_queue_tail = 0;
	if (current_thread->fs_base != 0) {
		aos_tcb_t* tcb = (aos_tcb_t*)current_thread->fs_base;
		if (tcb->pending_msgs > 0) tcb->pending_msgs--;
	}
	*out_msg = node->msg;
	kernel_free(node);
	return 0;
}

int64_t ipc_try_receive(message_t* out_msg) {
    asm volatile("cli");
    int res = __ipc_pop_msg(out_msg);
    asm volatile("sti");
    return res ? SYS_RES_QUEUE_EMPTY : SYS_RES_OK;
}

int64_t ipc_receive(message_t* out_msg) {
    while (1) {
        asm volatile("cli");
        if (!__ipc_pop_msg(out_msg)) {
            asm volatile("sti");
            return SYS_RES_OK;
        }
        current_thread->waiting_for_msg = 1;
        current_thread->state = THREAD_BLOCKED;
		schedule();
		asm volatile("sti");
    }
}
