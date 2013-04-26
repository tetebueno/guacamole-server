
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is libguac-client-ssh.
 *
 * The Initial Developer of the Original Code is
 * Michael Jumper.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * James Muehlner <dagger10k@users.sourceforge.net>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <guacamole/socket.h>
#include <guacamole/protocol.h>
#include <guacamole/client.h>

#include "types.h"
#include "buffer.h"
#include "display.h"
#include "terminal.h"
#include "terminal_handlers.h"

guac_terminal* guac_terminal_create(guac_client* client,
        int width, int height) {

    guac_terminal_char default_char = {
        .value = ' ',
        .attributes = {
            .foreground = 7,
            .background = 0,
            .bold       = false,
            .reverse    = false,
            .underscore = false
        }};

    guac_terminal* term = malloc(sizeof(guac_terminal));
    term->client = client;

    /* Init buffer */
    term->buffer = guac_terminal_buffer_alloc(1000, &default_char);
    term->scroll_offset = 0;

    /* Init display */
    term->display = guac_terminal_display_alloc(client,
            80, 24, /*term->term_width, term->term_height,*/
            default_char.attributes.foreground,
            default_char.attributes.background);

    /* Init terminal state */
    term->current_attributes = default_char.attributes;
    term->default_char = default_char;

    term->cursor_row = 0;
    term->cursor_col = 0;

    term->term_width   = 80; /*width  / term->display->char_width;*/
    term->term_height  = 24; /*height / term->display->char_height;*/
    term->char_handler = guac_terminal_echo; 

    term->scroll_start = 0;
    term->scroll_end = term->term_height - 1;

    term->text_selected = false;

    /* Init terminal lock */
    pthread_mutex_init(&(term->lock), NULL);

    return term;

}

void guac_terminal_free(guac_terminal* term) {
    
    /* Free display */
    guac_terminal_display_free(term->display);

    /* Free buffer */
    guac_terminal_buffer_free(term->buffer);

}

int guac_terminal_set(guac_terminal* term, int row, int col, char c) {

    /* Build character with current attributes */
    guac_terminal_char guac_char;
    guac_char.value = c;
    guac_char.attributes = term->current_attributes;

    guac_terminal_set_columns(term, row, col, col, &guac_char);

    return 0;

}

int guac_terminal_toggle_reverse(guac_terminal* term, int row, int col) {

    guac_terminal_char* guac_char;
    int scrolled_row = row + term->scroll_offset;

    /* Get character from buffer */
    guac_terminal_buffer_row* buffer_row = guac_terminal_buffer_get_row(term->buffer, row, col+1);

    /* Toggle reverse */
    guac_char = &(buffer_row->characters[col]);
    guac_char->attributes.reverse = !(guac_char->attributes.reverse);

    /* Set display */
    if (scrolled_row < term->display->height)
        guac_terminal_display_set_columns(term->display, scrolled_row, col, col, guac_char);

    return 0;

}

int guac_terminal_write(guac_terminal* term, const char* c, int size) {

    while (size > 0) {
        term->char_handler(term, *(c++));
        size--;
    }

    return 0;

}

int guac_terminal_scroll_up(guac_terminal* term,
        int start_row, int end_row, int amount) {

    /* Copy row data upwards */
    guac_terminal_copy_rows(term, start_row + amount, end_row, -amount);

    /* Clear new area */
    guac_terminal_clear_range(term,
            end_row - amount + 1, 0,
            end_row, term->term_width - 1);

    return 0;
}

int guac_terminal_scroll_down(guac_terminal* term,
        int start_row, int end_row, int amount) {
    /* STUB */
    return 0;
}

int guac_terminal_clear_columns(guac_terminal* term,
        int row, int start_col, int end_col) {

    /* Build space */
    guac_terminal_char blank;
    blank.value = ' ';
    blank.attributes = term->current_attributes;

    /* Clear */
    guac_terminal_set_columns(term,
        row, start_col, end_col, &blank);

    return 0;

}

int guac_terminal_clear_range(guac_terminal* term,
        int start_row, int start_col,
        int end_row, int end_col) {

    /* If not at far left, must clear sub-region to far right */
    if (start_col > 0) {

        /* Clear from start_col to far right */
        guac_terminal_clear_columns(term,
            start_row, start_col, term->term_width - 1);

        /* One less row to clear */
        start_row++;
    }

    /* If not at far right, must clear sub-region to far left */
    if (end_col < term->term_width - 1) {

        /* Clear from far left to end_col */
        guac_terminal_clear_columns(term, end_row, 0, end_col);

        /* One less row to clear */
        end_row--;

    }

    /* Remaining region now guaranteed rectangular. Clear, if possible */
    if (start_row <= end_row) {

        int row;
        for (row=start_row; row<=end_row; row++) {

            /* Clear entire row */
            guac_terminal_clear_columns(term, row, 0, term->term_width - 1);

        }

    }

    return 0;

}

void guac_terminal_scroll_display_down(guac_terminal* terminal,
        int scroll_amount) {

    int start_row, end_row;
    int dest_row;
    int row, column;

    /* Limit scroll amount by size of scrollback buffer */
    if (scroll_amount > terminal->scroll_offset)
        scroll_amount = terminal->scroll_offset;

    /* If not scrolling at all, don't bother trying */
    if (scroll_amount == 0)
        return;

    /* Shift screen up */
    if (terminal->term_height > scroll_amount)
        guac_terminal_display_copy_rows(terminal->display,
                scroll_amount, terminal->term_height - 1,
                -scroll_amount);

    /* Advance by scroll amount */
    terminal->scroll_offset -= scroll_amount;

    /* Get row range */
    end_row   = terminal->term_height - terminal->scroll_offset - 1;
    start_row = end_row - scroll_amount + 1;
    dest_row  = terminal->term_height - scroll_amount;

    /* Draw new rows from scrollback */
    for (row=start_row; row<=end_row; row++) {

        /* Get row from scrollback */
        guac_terminal_buffer_row* buffer_row =
            guac_terminal_buffer_get_row(terminal->buffer, row, 0);

        /* Draw row */
        /* FIXME: Clear row first */
        guac_terminal_char* current = buffer_row->characters;
        for (column=0; column<buffer_row->length; column++)
            guac_terminal_display_set_columns(terminal->display,
                    dest_row, column, column, current++);

        /* Next row */
        dest_row++;

    }

    /* FIXME: Should flush somewhere more sensible */
    guac_terminal_display_flush(terminal->display);
    guac_socket_flush(terminal->client->socket);

}

void guac_terminal_scroll_display_up(guac_terminal* terminal,
        int scroll_amount) {

    int start_row, end_row;
    int dest_row;
    int row, column;


    /* Limit scroll amount by size of scrollback buffer */
    if (terminal->scroll_offset + scroll_amount > terminal->buffer->length)
        scroll_amount = terminal->buffer->length - terminal->scroll_offset;

    /* If not scrolling at all, don't bother trying */
    if (scroll_amount == 0)
        return;

    /* Shift screen down */
    if (terminal->term_height > scroll_amount)
        guac_terminal_display_copy_rows(terminal->display,
                0, terminal->term_height - scroll_amount - 1,
                scroll_amount);

    /* Advance by scroll amount */
    terminal->scroll_offset += scroll_amount;

    /* Get row range */
    start_row = -terminal->scroll_offset;
    end_row   = start_row + scroll_amount - 1;
    dest_row  = 0;

    /* Draw new rows from scrollback */
    for (row=start_row; row<=end_row; row++) {

        /* Get row from scrollback */
        guac_terminal_buffer_row* buffer_row = 
            guac_terminal_buffer_get_row(terminal->buffer, row, 0);

        /* Draw row */
        /* FIXME: Clear row first */
        guac_terminal_char* current = buffer_row->characters;
        for (column=0; column<buffer_row->length; column++)
            guac_terminal_display_set_columns(terminal->display,
                    dest_row, column, column, current++);

        /* Next row */
        dest_row++;

    }

    /* FIXME: Should flush somewhere more sensible */
    guac_terminal_display_flush(terminal->display);
    guac_socket_flush(terminal->client->socket);

}

void guac_terminal_select_start(guac_terminal* terminal, int row, int column) {
    /* STUB */
}

void guac_terminal_select_update(guac_terminal* terminal, int row, int column) {
    /* STUB */
}

void guac_terminal_select_end(guac_terminal* terminal) {
    /* STUB */
}

void guac_terminal_copy_columns(guac_terminal* terminal, int row,
        int start_column, int end_column, int offset) {
    /* STUB */

    guac_terminal_display_copy_columns(terminal->display, row,
            start_column, end_column, offset);

    guac_terminal_buffer_copy_columns(terminal->buffer, row,
            start_column, end_column, offset);

}

void guac_terminal_copy_rows(guac_terminal* terminal,
        int start_row, int end_row, int offset) {
    /* STUB */

    guac_terminal_display_copy_rows(terminal->display,
            start_row, end_row, offset);

    guac_terminal_buffer_copy_rows(terminal->buffer,
            start_row, end_row, offset);

}

void guac_terminal_set_columns(guac_terminal* terminal, int row,
        int start_column, int end_column, guac_terminal_char* character) {
    /* STUB */

    guac_terminal_display_set_columns(terminal->display, row,
            start_column, end_column, character);

    guac_terminal_buffer_set_columns(terminal->buffer, row,
            start_column, end_column, character);

}

