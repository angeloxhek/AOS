#include <kernel/internal.h>

// -------------------------
//           IPC
// -------------------------

int64_t ipc_forward(apid_t dest_pid, message_t* user_msg) {
	if (!user_msg) return SYS_RES_INVALID;
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

    if (target->peb_phys_page != 0) {
        void* kvirt = temp_map(target->peb_phys_page);
        aos_peb_t* peb = (aos_peb_t*)kvirt;

        if (peb->pending_msgs >= target->ipc_queue_limit) {
            temp_unmap(kvirt);
            hal_irq_restore(irq);
            kernel_free(node);
            return SYS_RES_ALREADY;
        }
        temp_unmap(kvirt);
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

static uint64_t id = 1;
static spinlock_t idsp;

int64_t ipc_send(apid_t dest_pid, message_t* user_msg) {
	if (!user_msg) return SYS_RES_INVALID;
	spinlock_acquire(&idsp);
    user_msg->sender_pid = current_thread->owner->id;
	user_msg->id = id++;
	spinlock_release(&idsp);
	int64_t res = ipc_forward(dest_pid, user_msg);
    return (res == SYS_RES_OK) ? user_msg->id : res;
}

int64_t ipc_reply(message_t* in_msg, message_t* out_msg) {
	if (!in_msg || !out_msg) return SYS_RES_INVALID;
    out_msg->sender_pid = current_thread->owner->id;
	out_msg->id = in_msg->id;
    int64_t res = ipc_forward(in_msg->sender_pid, out_msg);
    return (res == SYS_RES_OK) ? out_msg->id : res;
}

static int __ipc_pop_msg(message_t* out_msg) {
    if (!current_thread->owner->msg_queue_head) return -1;
    msg_node_t* node = current_thread->owner->msg_queue_head;
    current_thread->owner->msg_queue_head = node->next;

    if (!current_thread->owner->msg_queue_head) current_thread->owner->msg_queue_tail = 0;

    if (current_thread->owner->peb_phys_page != 0) {
        void* kvirt = temp_map(current_thread->owner->peb_phys_page);
        aos_peb_t* peb = (aos_peb_t*)kvirt;

        if (peb->pending_msgs > 0)
            peb->pending_msgs--;

        temp_unmap(kvirt);
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
