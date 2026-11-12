#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using Minutes = std::chrono::minutes;

Minutes parse_time(const std::string& value)
{
	const std::size_t separator = value.find(':');
	if (separator == std::string::npos || value.find(':', separator + 1) != std::string::npos)
		throw std::invalid_argument("time must use HH:MM format");

	const int hours = std::stoi(value.substr(0, separator));
	const int minutes = std::stoi(value.substr(separator + 1));
	if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59)
		throw std::invalid_argument("time must be between 00:00 and 23:59");

	return std::chrono::hours(hours) + Minutes(minutes);
}

std::tm parse_date(const std::string& value)
{
	std::tm date = {};
	std::istringstream input(value);
	input >> std::get_time(&date, "%d-%m-%Y");
	if (input.fail() || input.peek() != std::char_traits<char>::eof())
		throw std::invalid_argument("date must use DD-MM-YYYY format");

	date.tm_isdst = -1;
	const int year = date.tm_year;
	const int month = date.tm_mon;
	const int day = date.tm_mday;
	const std::time_t timestamp = std::mktime(&date);
	if (timestamp == static_cast<std::time_t>(-1))
		throw std::invalid_argument("invalid date");
	const std::tm* normalized = std::localtime(&timestamp);
	if (normalized == NULL || normalized->tm_year != year || normalized->tm_mon != month || normalized->tm_mday != day)
		throw std::invalid_argument("invalid date");
	return *normalized;
}

std::time_t make_timestamp(std::tm date, Minutes time)
{
	date.tm_hour = static_cast<int>(time.count() / 60);
	date.tm_min = static_cast<int>(time.count() % 60);
	date.tm_sec = 0;
	date.tm_isdst = -1;
	const std::time_t timestamp = std::mktime(&date);
	if (timestamp == static_cast<std::time_t>(-1))
		throw std::runtime_error("could not convert shift date and time");
	return timestamp;
}

bool read_last_shift_end(std::time_t& timestamp)
{
	std::ifstream file("info.txt");
	if (!file)
		return false;
	std::string value;
	if (!std::getline(file, value) || value.empty())
		return false;
	if (value.size() != 16 || value[10] != ' ')
		throw std::runtime_error("info.txt must contain a date and time like DD-MM-YYYY HH:MM");
	timestamp = make_timestamp(parse_date(value.substr(0, 10)), parse_time(value.substr(11)));
	return true;
}

void save_last_shift_end(std::time_t timestamp)
{
	const std::tm* end_date = std::localtime(&timestamp);
	if (end_date == NULL)
		throw std::runtime_error("could not format shift end date and time");
	char value[17];
	if (std::strftime(value, sizeof(value), "%d-%m-%Y %H:%M", end_date) == 0)
		throw std::runtime_error("could not format shift end date and time");
	std::ofstream file("info.txt", std::ios::trunc);
	if (!file || !(file << value << '\n'))
		throw std::runtime_error("could not save shift end to info.txt");
}

typedef struct s_WorkShift {
	double day_rate; // 
	double hourly_rate;
	Minutes start_time; // minutes after midnight
	Minutes end_time;   // minutes after midnight
	Minutes hours_worked; // elapsed time worked
	Minutes short_rest_overtime;
	double pay; // total pay for the shift
	int break_interruption; // number of interrupted 10-minute breaks
} WorkShift;

void print_shift(const WorkShift& shift)
{
	std::cout << "Day rate: Euro" << shift.day_rate << std::endl;
	std::cout << "Hourly rate: Euro " << shift.hourly_rate << std::endl;
	std::cout << "Start time: " << std::chrono::duration_cast<std::chrono::hours>(shift.start_time).count() << ":"
		<< std::chrono::duration_cast<Minutes>(shift.start_time % std::chrono::hours(1)).count() << std::endl;
	std::cout << "End time: " << std::chrono::duration_cast<std::chrono::hours>(shift.end_time).count() << ":"
		<< std::chrono::duration_cast<Minutes>(shift.end_time % std::chrono::hours(1)).count() << std::endl;
	std::cout << "Hours worked: " << std::chrono::duration_cast<std::chrono::hours>(shift.hours_worked).count() << "h "
		<< std::chrono::duration_cast<Minutes>(shift.hours_worked % std::chrono::hours(1)).count() << "m" << std::endl;
	std::cout << "Interrupted breaks: " << shift.break_interruption << std::endl;
	if (shift.short_rest_overtime > Minutes::zero())
		std::cout << "Short-rest overtime: "
			<< std::chrono::duration_cast<std::chrono::hours>(shift.short_rest_overtime).count() << "h "
			<< (shift.short_rest_overtime % std::chrono::hours(1)).count() << "m" << std::endl;
	std::cout << "Total pay: Euro " << shift.pay << std::endl;
}

void pay_calculation(WorkShift& shift)
{
	shift.hours_worked += shift.short_rest_overtime; // add short-rest overtime to hours worked
	if (shift.hours_worked <= std::chrono::hours(12))
	{
		shift.pay += shift.day_rate;
	}
	else
	{
		shift.pay += shift.day_rate; // pay for the first 12 hours
		Minutes overtime = shift.hours_worked - std::chrono::hours(12);
		Minutes tmp = overtime % std::chrono::hours(1);
		if (tmp > std::chrono::minutes(5) && tmp <= std::chrono::minutes(14))
		{
			Minutes rounded = std::chrono::minutes(15) - tmp; // round up to the next 10 minutes
			shift.hours_worked += rounded; // round up to the next 10 minutes
			overtime += rounded; // round up to the next hour
		}
		else if (tmp > std::chrono::minutes(14))
		{
			Minutes rounded = std::chrono::hours(1) - tmp; // round up to the next hour
			shift.hours_worked += rounded; // round up to the next hour
			overtime += rounded; // round up to the next hour
		}
		shift.pay += std::chrono::duration_cast<std::chrono::minutes>(overtime).count() * ((shift.hourly_rate / 60) * 1.5); // pay for overtime at 1.5x rate
	}
	if (shift.break_interruption > 0)
	{
		shift.hours_worked += shift.break_interruption * std::chrono::minutes(1); // add interrupted breaks to hours worked
		shift.pay += shift.break_interruption * ((shift.hourly_rate * 1.5) / 60); // pay for interrupted breaks
	}

}


int main (int argc, char *argv[])
{
	// data
	// kolko pari na 12 chasov rabotenden
	// v kolko chasa zapochvash
	// v kolko prikluchvash
	// ako e narushena pausata s kolko inache 0
	// ako mejdu dve smeni nqma 12 chasa *skrit overtime (ne e parameter)

	//over 12 h > kolko 1.5 .h
	// break rules
	// every 10 mins sa plus 10 min overtime pay ontop of worked hours
	// fuck the 120 min exeption (mn slojno)

	if (argc != 6) {
		std::cerr << "Usage: " << argv[0] << " <day_rate> <start_time(HH:MM)> <end_time(HH:MM)> <interrupted_10_min_breaks> <date(DD-MM-YYYY)>" << std::endl;
		return 1;
	}
	WorkShift shift;
	std::time_t shift_end;
	try {
		shift.day_rate = std::stod(argv[1]);
		shift.start_time = parse_time(argv[2]);
		shift.end_time = parse_time(argv[3]);
		shift.break_interruption = std::stoi(argv[4]);
		shift.pay = 0;
		shift.short_rest_overtime = Minutes::zero();
		if (shift.day_rate < 0 || shift.break_interruption < 0)
			throw std::invalid_argument("day rate and interrupted-break count cannot be negative");
		shift.hourly_rate = shift.day_rate / 12; // Assuming a standard 12-hour workday
		const std::tm start_date = parse_date(argv[5]);
		shift.hours_worked = shift.end_time - shift.start_time;
		if (shift.hours_worked < Minutes::zero())
			shift.hours_worked += std::chrono::hours(24); // The shift ended the following day

		const std::time_t shift_start = make_timestamp(start_date, shift.start_time);
		std::tm end_date = start_date;
		if (shift.end_time < shift.start_time)
			++end_date.tm_mday;
		shift_end = make_timestamp(end_date, shift.end_time);

		std::time_t previous_shift_end;
		if (read_last_shift_end(previous_shift_end))
		{
			const double rest_seconds = std::difftime(shift_start, previous_shift_end);
			if (rest_seconds < 0)
				throw std::invalid_argument("shift starts before the saved previous shift ends; check the date or info.txt");
			const Minutes rest = std::chrono::duration_cast<Minutes>(
				std::chrono::seconds(static_cast<long long>(rest_seconds)));
			if (rest < std::chrono::hours(12))
				shift.short_rest_overtime = std::chrono::hours(12) - rest;
			else
				std::cout << "Rest since previous shift: at least 12h\n";
		}
		else
			std::cout << "No previous shift found in info.txt.\n";
	} 
	catch (const std::exception& error) {
		std::cerr << "Error: " << error.what() << std::endl;
		return 1;
	}
	try {
		save_last_shift_end(shift_end);
	}
	catch (const std::exception& error) {
		std::cerr << "Error saving shift end: " << error.what() << std::endl;
		return 1;
	}
	pay_calculation(shift);
	print_shift(shift);
    return 0;
}