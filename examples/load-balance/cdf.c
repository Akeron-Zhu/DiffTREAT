#include "cdf.h"

/* initialize a CDF distribution */
//初始化一张表出来，类似于vector先定好大小，后面不够再增加
void init_cdf(struct cdf_table *table)
{
    if(!table)
        return;

    table->entries = (struct cdf_entry*)malloc(TG_CDF_TABLE_ENTRY * sizeof(struct cdf_entry));
    table->num_entry = 0;
    table->max_entry = TG_CDF_TABLE_ENTRY;
    table->min_cdf = 0;
    table->max_cdf = 1;

    if (!(table->entries))
        perror("Error: malloc entries in init_cdf()");
}

/* free resources of a CDF distribution */
//释放这张表占用的资源
void free_cdf(struct cdf_table *table)
{
    if (table)
        free(table->entries);
}

/* get CDF distribution from a given file */
void load_cdf(struct cdf_table *table, const char *file_name)
{
    FILE *fd = NULL;
    char line[256] = {0};
    struct cdf_entry *e = NULL;
    int i = 0;

    if (!table)
        return;
    //打开文件 
    fd = fopen(file_name, "r");
    if (!fd)
        perror("Error: open the CDF file in load_cdf()");
    //每次从文件中读取一行到line数组中
    while (fgets(line , sizeof(line), fd))
    {
        /* resize entries */
        //如果表的大小不够用了就扩大一倍
        if (table->num_entry >= table->max_entry)
        {
            table->max_entry *= 2;
            e = (struct cdf_entry*)malloc(table->max_entry * sizeof(struct cdf_entry));
            if (!e)
                perror("Error: malloc entries in load_cdf()");
            for (i = 0; i < table->num_entry; i++)
                e[i] = table->entries[i];
            free(table->entries);
            table->entries = e;
        }
        //从line中读取两个值
        sscanf(line, "%lf %lf", &(table->entries[table->num_entry].value), &(table->entries[table->num_entry].cdf));
        //如果超过了范围则进行纠正
        if (table->min_cdf > table->entries[table->num_entry].cdf)
            table->min_cdf = table->entries[table->num_entry].cdf;
        if (table->max_cdf < table->entries[table->num_entry].cdf)
            table->max_cdf = table->entries[table->num_entry].cdf;

        table->num_entry++;
    }
    fclose(fd);
}

/* print CDF distribution information */
//显示表中的数据
void print_cdf(struct cdf_table *table)
{
    int i = 0;

    if (!table)
        return;

    for (i = 0; i < table->num_entry; i++)
        printf("%.2f %.2f\n", table->entries[i].value, table->entries[i].cdf);
}

/* get average value of CDF distribution */
double avg_cdf(struct cdf_table *table)
{
    int i = 0;
    double avg = 0;
    double value, prob;

    if (!table)
        return 0;

    for (i = 0; i < table->num_entry; i++)
    {
        if (i == 0)
        {
            value = table->entries[i].value / 2;
            prob = table->entries[i].cdf;
        }
        else
        {
            //注意是CDF分布，所以要减去上一条的概率，不用计算下面的一部分均值
            value = (table->entries[i].value + table->entries[i-1].value) / 2;
            prob = table->entries[i].cdf - table->entries[i-1].cdf;
        }
        avg += (value * prob);
    }

    return avg;
}

double interpolate(double x, double x1, double y1, double x2, double y2)
{
    if (x1 == x2)
        return (y1 + y2) / 2;
    else
        return y1 + (x - x1) * (y2 - y1) / (x2 - x1);
}

/* generate a random floating point number from min to max */
//产生介于min与max之间的随机数
double rand_range(double min, double max)
{
    return min + rand() * (max - min) / RAND_MAX;
}

/* generate a random value based on CDF distribution */
//基于CDF产生一个value值，随机产生一个介于0-1的cdf值，然后找到其对应的value值。
double gen_random_cdf(struct cdf_table *table)
{
    int i = 0;
    double x = rand_range(table->min_cdf, table->max_cdf);
    /* printf("%f %f %f\n", x, table->min_cdf, table->max_cdf); */

    if (!table)
        return 0;

    for (i = 0; i < table->num_entry; i++)
    {
        if (x <= table->entries[i].cdf)
        {
            if (i == 0)
                return interpolate(x, 0, 0, table->entries[i].cdf, table->entries[i].value);
            else
                return interpolate(x, table->entries[i-1].cdf, table->entries[i-1].value, table->entries[i].cdf, table->entries[i].value);
        }
    }

    return table->entries[table->num_entry-1].value;
}
