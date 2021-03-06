
#include "readvariant.h"
#include "readfasta.h"
#include <assert.h>
#include "htslib/hts.h" // read bgzipped VCF files

// count the # of variants in VCF file to allocate space for VCF variant array

int count_variants(char* vcffile, char* sampleid, int* samplecol) {
	FILE* fp = fopen(vcffile, "r");
	if (fp == NULL) {
		fprintf(stderr, "could not open file %s\n\n", vcffile);
		return -1;
	}
	int variants = 0;
	char buffer[500000];
	int i = 0, j = 0, cols = 0;

	while (fgets(buffer, 500000, fp)) {
		if (buffer[0] != '#') variants++; // this should work for non-VCF files as well.
		else if (buffer[0] == '#' && buffer[1] == '#') continue;
		else if (buffer[0] == '#' && buffer[1] == 'C' && buffer[2] == 'H' && buffer[3] == 'R' && buffer[4] == 'O' && buffer[5] == 'M') {
			// find the column of the sample we want to phase
			j = 0;
			while (buffer[i++] != '\n') {
				if ((buffer[i] == ' ' || buffer[i] == '\t') && j == 1) j = 0;
				else if (buffer[i] != ' ' && buffer[i] != '\t' && buffer[i] != '\n' && j == 0) {
					j = 1;
					cols++;
				}
			}
		}
	}
	fclose(fp);
	fprintf(stderr, "VCF file %s has %d variants \n", vcffile, variants);
	return variants;
}

// split a string using a single separator, '\n' and '\0' are also delimitors 
int splitString(char* input,char sep,char** var_list)
{
        int n=0,i=0,s=0,j=0;
        while (1)
        {
                if ( (input[i] == sep || input[i] == '\n' || input[i] == '\0') && (i-s)> 0)
                {
                        var_list[n] = malloc(i-s+1);
                        for (j = s; j < i; j++) 
			{
				var_list[n][j - s] = input[j];
			}
			var_list[n][j-s] = '\0';
                        s = i+1; n +=1; // start of next string
                }
                if (input[i] =='\n' || input[i] == '\0') break;
                i++;
        }
        return n;
}

int parse_variant(VARIANT* variant, char* buffer, int samplecol) 
{
	int i = 0, j = 0, k = 0;
	int flag = 0;
	// additional variables so can calculate genotype likelihoods and allele counts for each variant using all reads not just haplotype-informative reads
	variant->depth = 0;
	variant->A1 = 0;
	variant->A2 = 0;
	int l0=0,l1=0;

        char** string_list = (char**)malloc(sizeof(char*)*4096); // if VCF file has more than 4096 columns... 
	//char** alist = (char**)malloc(sizeof(char*)*16);
	k = splitString(buffer,'\t',string_list);
        if (k < samplecol)
        {
                fprintf(stderr,"ERROR reading VCF file: less than %d columns in VCF file\n",samplecol);
		for (i=0;i<k;i++) free(string_list[i]); // free array of split strings
		free(string_list);
                return -1;
        }
        variant->chrom = (char*)malloc(strlen(string_list[0])+1); strcpy(variant->chrom,string_list[0]);
	variant->position = atoi(string_list[1]);
	l0 = strlen(string_list[3]);  l1 = strlen(string_list[4]);  // length of alleles
	variant->RA =  (char*)malloc(l0+1); strcpy(variant->RA,string_list[3]);
	variant->AA =  (char*)malloc(l1+1); strcpy(variant->AA,string_list[4]);
	// assert that GT field is the first field
	assert(string_list[8][0] == 'G' && string_list[8][1] == 'T');
	variant->genotype=(char*)malloc(strlen(string_list[samplecol-1])+1); strcpy(variant->genotype,string_list[samplecol-1]);

        for (i=0;i<k;i++) free(string_list[i]); // free array of split strings
	free(string_list);
	// split genotype to get the sub-fields, split variant->AA to get the alternate alleles 
 //       int altalleles = splitString(variant->AA,',',alist);

	int gl = strlen(variant->genotype);
        //fprintf(stderr,"variant genotype %d %s \n",variant->position,variant->genotype);
	// check that the genotype field is diploid
	if ((gl >= 3 && (variant->genotype[1] == '/' || variant->genotype[1] == '|')) &&
			(gl == 3 || variant->genotype[3] == ':') &&
			(variant->genotype[0] == '0' || variant->genotype[0] == '1' || variant->genotype[0] == '2') &&
			(variant->genotype[2] == '0' || variant->genotype[2] == '1' || variant->genotype[2] == '2')){

		if (variant->genotype[0] != '2' && variant->genotype[2] != '2') // both alleles are 0/1
		{
			variant->allele1 = (char*) malloc(strlen(variant->RA) + 1);
			strcpy(variant->allele1, variant->RA);
			j = 0;
			while (variant->AA[j] != ',' && j < strlen(variant->AA)) j++;
			variant->allele2 = (char*) malloc(j + 1);
			for (i = 0; i < j; i++) variant->allele2[i] = variant->AA[i];
			variant->allele2[i] = '\0';
			variant->type = strlen(variant->allele2) - strlen(variant->allele1);
		} else if (variant->genotype[0] == '0' || variant->genotype[2] == '0') // at least one allele is reference
		{
			variant->allele1 = (char*) malloc(strlen(variant->RA) + 1);
			strcpy(variant->allele1, variant->RA);
			j = 0;
			while (variant->AA[j] != ',' && j < strlen(variant->AA)) j++;
			k = j + 1;
			while (variant->AA[k] != ',' && k < strlen(variant->AA)) k++;
			variant->allele2 = (char*) malloc(k - j + 1);
			for (i = j + 1; i < k; i++) variant->allele2[i - j - 1] = variant->AA[i];
			variant->allele2[i - j - 1] = '\0';
			variant->type = strlen(variant->allele2) - strlen(variant->allele1);
		} else // reference allele is missing 1/2 case
		{
			j = 0;
			while (variant->AA[j] != ',' && j < strlen(variant->AA)) j++;
			variant->allele1 = (char*) malloc(j + 1);
			for (i = 0; i < j; i++) variant->allele1[i] = variant->AA[i];
			variant->allele1[i] = '\0';
			k = j + 1;
			while (variant->AA[k] != ',' && k < strlen(variant->AA)) k++;
			variant->allele2 = (char*) malloc(k - j + 1);
			for (i = j + 1; i < k; i++) variant->allele2[i - j - 1] = variant->AA[i];
			variant->allele2[i - j - 1] = '\0';
			variant->type = strlen(variant->allele2) - strlen(variant->allele1);
			//fprintf(stderr,"non-reference het variant %s %s \n",variant->allele1,variant->allele2);
			// need to allow for multiple alternate alleles or discard ones where there is a comma in alternate allele list
			//j=0; while (variant->AA[j] != ',' && j < strlen(variant->AA)) j++;
			//variant->allele1 = (char*)malloc(j+1); for (i=0;i<j;i++) variant->allele1[i] = variant->AA[i]; variant->allele1[i] = '\0';
			//variant->type = strlen(variant->allele2) -strlen(variant->allele1);
		}

		// reduce the length of the two alleles for VCF format outputted by samtoools, april 17 2012
		// basically CAAAA -> CAA  can be reduced to CA -> C
		i = strlen(variant->allele1) - 1;
		j = strlen(variant->allele2) - 1;
		flag = 0;
		while (i > 0 && j > 0) {
			if (variant->allele1[i] != variant->allele2[j]) break;
			i--;
			j--;
			flag++;
		}
		variant->allele1[i + 1] = '\0';
		variant->allele2[j + 1] = '\0';

		if (variant->type != 0) variant->position++; // add one to position for indels

		//if (strlen(variant->genotype))
		if ((variant->genotype[0] == '0' && variant->genotype[2] == '1') || (variant->genotype[0] == '1' && variant->genotype[2] == '0')) {
			//if (flag >0) fprintf(stderr,"%s %d %s %s \n",variant->chrom,variant->position,variant->allele1,variant->allele2);
			variant->heterozygous = '1'; // variant will be used for outputting hairs
			//fprintf(stdout,"variant %s %s %s %c\n",variant->allele1,variant->allele2,variant->genotype,variant->heterozygous);
			return 1;
		}
		if ((variant->genotype[0] == '0' && variant->genotype[2] == '2') || (variant->genotype[0] == '2' && variant->genotype[2] == '0')) {
			//if (flag >0) fprintf(stderr,"%s %d %s %s \n",variant->chrom,variant->position,variant->allele1,variant->allele2);
			variant->heterozygous = '1'; // variant will be used for outputting hairs
			//fprintf(stdout,"variant %s %s %s %c\n",variant->allele1,variant->allele2,variant->genotype,variant->heterozygous);
			return 1;
		} else if (variant->genotype[0] != variant->genotype[2] && variant->genotype[0] != '.' && variant->genotype[2] != '.') {
			if (TRI_ALLELIC == 1) variant->heterozygous = '2';
			else variant->heterozygous = '3';
			//fprintf(stderr, "non-ref het variant %d %s %s %s %s %s\n", variant->position, variant->RA, variant->AA, variant->allele1, variant->allele2, variant->genotype);
			// if both alleles are different from reference, we ignore it
			return -1;
		} else {
			variant->heterozygous = '0';
			//if (variant->genotype[0] == variant->genotype[2] && variant->genotype[0] =='1') variant->heterozygous = '1';
			return 0;
		}
	} 
        else 
        {
		fprintf(stdout, "\nERROR: Non-diploid VCF entry detected. Each VCF entry must have a diploid genotype (GT) field consisting of two alleles in the set {0,1,2} separated by either \'/\' or \'|\'. For example, \"1/1\", \"0/1\", and \"0|2\" are valid diploid genotypes for HapCUT2, but \"1\", \"0/3\", and \"0/0/1\" are not.\nThe invalid entry is: \n\n%s\n", buffer);
		exit(1);
	}
	//free(variant->genotype); free(variant->AA); free(variant->RA); free(variant->chrom);
}

int read_variantfile(char* vcffile, VARIANT* varlist, HASHTABLE* ht, int* hetvariants, int samplecol) {
	FILE* fp = fopen(vcffile, "r");
	char buffer[500000];
	int i = 0;
	//	char allele1[256]; char allele2[256]; char genotype[256]; int quality;
	char prevchrom[256];
	strcpy(prevchrom, "----");
	int chromosomes = 0; //int blocks=0;
	*hetvariants = 0;
	int het = 0;
	int non_biallelic =0;

	while (fgets(buffer, 500000, fp)) {
		if (buffer[0] == '#') continue;
		else {
			het = parse_variant(&varlist[i], buffer, samplecol);
			if (het > 0) (*hetvariants) += het;
			if (het == -1) non_biallelic++;
			//if (het ==0) continue; else (*hetvariants)++;
			//	fprintf(stdout,"%s %d %s %s %s %s\n",varlist[i].chrom,varlist[i].position,varlist[i].RA,varlist[i].AA,varlist[i].genotype,prevchrom);
			if (strcmp(varlist[i].chrom, prevchrom) != 0) {
				//	fprintf(stderr,"chromosomes %d %d\n",chromosomes,i);
				// insert chromname into hashtable
				insert_keyvalue(ht, varlist[i].chrom, strlen(varlist[i].chrom), chromosomes);
				fprintf(stderr,"adding chrom %s to index \n",varlist[i].chrom);
				strcpy(prevchrom, varlist[i].chrom);
				chromosomes++;
			}
			i++;
		}
	}
	fclose(fp); //chromosomes--;
	fprintf(stderr, "vcffile %s chromosomes %d hetvariants %d variants %d \n", vcffile, chromosomes, *hetvariants, i);
	if (TRI_ALLELIC ==0) fprintf(stderr,"detected %d variants with two non-reference alleles, these variants will not be phased\n",non_biallelic);
	if (chromosomes > 1) 
        {
                fprintf(stderr,"\n##########################################################################################\n");
	        fprintf(stderr,"%d chromosomes/contigs detected in input VCF file\n",chromosomes);
		fprintf(stderr,"processing each contig separately using --regions option will be more efficient for large genomes\n");
		fprintf(stderr,"############################################################################################\n\n");
        }

	return chromosomes;

}

// build a physical map that maps  intervals on chromosomes to the first variant that precedes the start of that interval
// chromvars is an array of chromosomes 
void build_intervalmap(CHROMVARS* chromvars, int chromosomes, VARIANT* varlist, int snps) {
	int i = 0, j = 0, k = 0, blocks = 0;
	chromvars[j].first = 0;
	for (i = 0; i < snps - 1; i++) {
		if (strcmp(varlist[i].chrom, varlist[i + 1].chrom) != 0) { // different chromosomes...
			chromvars[j].last = i;
			chromvars[j].variants = chromvars[j].last - chromvars[j].first + 1;
			//fprintf(stderr,"new chrom %d %d %s %s\n",j,i+115,varlist[i].chrom,varlist[i+1].chrom);
			j++;
			chromvars[j].first = i + 1; // initialize next chromosome
		}
	}
	chromvars[j].last = i;
	//	int** intervalmap; // map 1000bp of chromosome to first snp in that region indexed by snp_array
	// first SNP to the right of the given base position including that position
	for (j = 0; j < chromosomes; j++) {
		blocks = (int) (varlist[chromvars[j].last].position / BSIZE) + 2;
		chromvars[j].blocks = blocks;
		//	fprintf(stderr,"chromosomes %d blocks %d \n",j,blocks);
		chromvars[j].intervalmap = (int*) malloc(sizeof (int)*blocks);
		for (i = 0; i < blocks; i++) chromvars[j].intervalmap[i] = -1;
		//fprintf(stderr,"blocks for chrom %d: %d \n",j,blocks);
		k = chromvars[j].first;
		for (i = 0; i < blocks; i++) {
			if (k == chromvars[j].last){
				chromvars[j].intervalmap[i] = k;
				continue;
			}
			while (varlist[k].position < BSIZE * i && k < chromvars[j].last) k++;
			if (varlist[k].position >= BSIZE * i && chromvars[j].intervalmap[i] == -1) chromvars[j].intervalmap[i] = k;
			//if (k == chromvars[j].last) break;

			//		if (chromvars[j].intervalmap[i] != -1) printf("FSNPtoright chrom %d block %d: %d %d \n",j,BSIZE*i,chromvars[j].intervalmap[i],varlist[chromvars[j].intervalmap[i]].position);
			//			else printf("FSNPtoright chrom %d block %d: %d \n",j,BSIZE*i,intervalmap[j][i]);
		}
	}
}

// this will only work for pure insertions and deletions, not for block substitutions
int calculate_rightshift(VARIANT* varlist, int ss, REFLIST* reflist) {
	int i = 0, j = 0;
	int a1 = 0, a2 = 0;
	int shift = 0;
	a1 = strlen(varlist[ss].allele1);
	a2 = strlen(varlist[ss].allele2);
	if (a1 > a2 && a2 == 1) 
        {
		i = varlist[ss].position; // first base of deletion assuming position is +1 and not previous base
		j = varlist[ss].position + a1 - a2;
		while (i - 1 < reflist->lengths[reflist->current] && j - 1 < reflist->lengths[reflist->current] && reflist->sequences[reflist->current][i - 1] == reflist->sequences[reflist->current][j - 1]) {
			i++;
			j++;
			shift++;
		}
		varlist[ss].shift = shift;
		return shift;
	} 
        else if (a1 == 1 && a2 > a1) 
        {
		i = 1;
		j = varlist[ss].position;
		while (j - 1 < reflist->lengths[reflist->current] && varlist[ss].allele2[i] == reflist->sequences[reflist->current][j - 1] && i < a2) {
			i++;
			j++;
			shift++;
		}
		if (i == a2) // covered the full length of the inserted bases
		{
			i = varlist[ss].position;
			while (i - 1 < reflist->lengths[reflist->current] && j - 1 < reflist->lengths[reflist->current] && reflist->sequences[reflist->current][i - 1] == reflist->sequences[reflist->current][j - 1]) {
				i++;
				j++;
				shift++;
			}
		}
		varlist[ss].shift = shift;
		return shift;
	} 
	else 
	{
		varlist[ss].shift=0;
		return 0;
	}
}



