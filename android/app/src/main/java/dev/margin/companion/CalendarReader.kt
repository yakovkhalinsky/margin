// SPDX-License-Identifier: MIT
// Reads calendar events from CalendarContract for the next 7 days.
package dev.margin.companion

import android.content.ContentResolver
import android.content.Context
import android.database.Cursor
import android.provider.CalendarContract
import android.util.Log
import dev.margin.protocol.CalendarEventData
import dev.margin.protocol.CalendarSyncData
import java.util.Calendar
import java.util.TimeZone

class CalendarReader(private val context: Context) {

    fun readUpcomingEvents(daysAhead: Int = 7): CalendarSyncData {
        val events = mutableListOf<CalendarEventData>()

        val now = System.currentTimeMillis()
        val endTime = now + daysAhead * 24L * 60 * 60 * 1000

        val projection = arrayOf(
            CalendarContract.Events._ID,
            CalendarContract.Events.TITLE,
            CalendarContract.Events.DTSTART,
            CalendarContract.Events.DTEND,
            CalendarContract.Events.EVENT_LOCATION,
            CalendarContract.Events.ALL_DAY,
            CalendarContract.Events.CALENDAR_ID
        )

        val selection = "(${CalendarContract.Events.DTSTART} >= ? AND ${CalendarContract.Events.DTSTART} <= ?) " +
                "OR (${CalendarContract.Events.DTEND} >= ? AND ${CalendarContract.Events.DTSTART} <= ?)"
        val selectionArgs = arrayOf(
            now.toString(), endTime.toString(),
            now.toString(), endTime.toString()
        )

        val sortOrder = "${CalendarContract.Events.DTSTART} ASC"

        var cursor: Cursor? = null
        try {
            cursor = context.contentResolver.query(
                CalendarContract.Events.CONTENT_URI,
                projection,
                selection,
                selectionArgs,
                sortOrder
            )

            cursor?.let {
                val idIdx = it.getColumnIndex(CalendarContract.Events._ID)
                val titleIdx = it.getColumnIndex(CalendarContract.Events.TITLE)
                val startIdx = it.getColumnIndex(CalendarContract.Events.DTSTART)
                val endIdx = it.getColumnIndex(CalendarContract.Events.DTEND)
                val locationIdx = it.getColumnIndex(CalendarContract.Events.EVENT_LOCATION)
                val allDayIdx = it.getColumnIndex(CalendarContract.Events.ALL_DAY)

                while (it.moveToNext() && events.size < 100) {
                    val id = if (idIdx >= 0) it.getLong(idIdx) else 0L
                    val title = if (titleIdx >= 0) it.getString(titleIdx) ?: "" else ""
                    val start = if (startIdx >= 0) it.getLong(startIdx) else 0L
                    var end = if (endIdx >= 0) it.getLong(endIdx) else start
                    val location = if (locationIdx >= 0) it.getString(locationIdx) ?: "" else ""
                    val allDay = if (allDayIdx >= 0) it.getInt(allDayIdx) == 1 else false

                    if (end == 0L) end = start + 3600000

                    if (title.isNotBlank()) {
                        events.add(CalendarEventData(
                            eventId = id,
                            title = title,
                            start = start,
                            end = end,
                            location = location,
                            allDay = allDay
                        ))
                    }
                }
            }
        } catch (e: SecurityException) {
            Log.w(TAG, "Calendar permission not granted", e)
        } catch (e: Exception) {
            Log.e(TAG, "Error reading calendar", e)
        } finally {
            cursor?.close()
        }

        events.sortBy { it.start }
        return CalendarSyncData(events.take(100))
    }

    fun readCalendars(): List<CalendarInfo> {
        val calendars = mutableListOf<CalendarInfo>()

        val projection = arrayOf(
            CalendarContract.Calendars._ID,
            CalendarContract.Calendars.CALENDAR_DISPLAY_NAME,
            CalendarContract.Calendars.ACCOUNT_NAME,
            CalendarContract.Calendars.VISIBLE
        )

        var cursor: Cursor? = null
        try {
            cursor = context.contentResolver.query(
                CalendarContract.Calendars.CONTENT_URI,
                projection,
                null, null, null
            )

            cursor?.let {
                val idIdx = it.getColumnIndex(CalendarContract.Calendars._ID)
                val nameIdx = it.getColumnIndex(CalendarContract.Calendars.CALENDAR_DISPLAY_NAME)
                val accountIdx = it.getColumnIndex(CalendarContract.Calendars.ACCOUNT_NAME)
                val visibleIdx = it.getColumnIndex(CalendarContract.Calendars.VISIBLE)

                while (it.moveToNext()) {
                    calendars.add(CalendarInfo(
                        id = if (idIdx >= 0) it.getLong(idIdx) else 0L,
                        name = if (nameIdx >= 0) it.getString(nameIdx) ?: "" else "",
                        account = if (accountIdx >= 0) it.getString(accountIdx) ?: "" else "",
                        visible = if (visibleIdx >= 0) it.getInt(visibleIdx) == 1 else true
                    ))
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error reading calendars", e)
        } finally {
            cursor?.close()
        }

        return calendars
    }

    data class CalendarInfo(
        val id: Long,
        val name: String,
        val account: String,
        val visible: Boolean
    )

    companion object {
        private const val TAG = "CalendarReader"
    }
}
